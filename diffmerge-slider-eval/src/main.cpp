#include <cmath>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <diffcore/DiffEngine.h>
#include <diffcore/SliderHeuristics.h>

#include "SliderEval.h"

using namespace diffmerge::slidereval;

static QStringList loadLines(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QStringList lines;
    while (!in.atEnd())
        lines.append(in.readLine());
    return lines;
}

static QString findPairFile(const QString& repoDir,
                            const QString& digest, int side) {
    const QString prefix = digest + QLatin1Char('_') + QString::number(side);
    const QStringList candidates =
        QDir(repoDir).entryList({prefix + QStringLiteral(".*")}, QDir::Files);
    if (candidates.isEmpty()) return {};
    return repoDir + QLatin1Char('/') + candidates.first();
}

static const char* kSep = "  +------------------------------------------\n";


// Print ctx lines of context before/after a block [pos..pos+size-1] (1-based).
// Block lines are prefixed with the diff sign ('+' / '-') and surrounded
// by a single pair of separator lines.
static void printFragment(QTextStream& out,
                          const QStringList& lines,
                          int pos, int blockSize, char sign, int ctx = 3) {
    const int blockEnd = pos + blockSize - 1;  // inclusive
    const int from = qMax(1, pos - ctx);
    const int to   = qMin(lines.size(), blockEnd + ctx);
    for (int i = from; i <= to; ++i) {
        const bool inBlock = (i >= pos && i <= blockEnd);
        if (i == pos) out << kSep;
        const char pfx = inBlock ? sign : ' ';
        out << QStringLiteral("  %1 %2  %3\n")
               .arg(pfx).arg(i, 5).arg(lines[i - 1]);
        if (i == blockEnd) out << kSep;
    }
}

static QString probeLabel(const diffcore::HeuristicProbe& probe) {
    static const char* names[] = {
        nullptr, "H1", "H2", "H3", "H3b", "H4", "H5", "H6", "H7"
    };
    const char* excl = (probe.exclusive != diffcore::HeuristicId::None)
                       ? names[static_cast<int>(probe.exclusive)] : nullptr;
    if (excl && probe.h1Applied)
        return QStringLiteral("%1+H1").arg(QLatin1String(excl));
    if (excl)
        return QLatin1String(excl);
    if (probe.h1Applied)
        return QStringLiteral("H1");
    return {};
}

static void printSliderDetail(QTextStream& out,
                              const SliderCase& sc,
                              const QString& digest,
                              int humanPos, int dmPos, int dmError,
                              int blockSize,
                              const QStringList& linesA,
                              const QStringList& linesB,
                              const diffcore::HeuristicProbe& probe = {}) {
    out << QStringLiteral("\n========================================\n");
    out << QStringLiteral("  digest=%1  %2 blockBegin=%3 delta=%4  blockSize=%5\n")
           .arg(digest).arg(QChar(sc.sign)).arg(sc.blockBegin).arg(sc.delta)
           .arg(blockSize);
    out << QStringLiteral("  git=%1  sys=%2  human=%3  dm=%4  dm_error=%5\n")
           .arg(sc.blockBegin).arg(sc.diffPos).arg(humanPos).arg(dmPos).arg(dmError);

    // For '+' sliders: relevant file is B (insertion visible in right file).
    // For '-' sliders: relevant file is A (deletion visible in left file).
    const QStringList& rel  = (sc.sign == '+') ? linesB : linesA;
    const char         side = (sc.sign == '+') ? 'B' : 'A';

    auto section = [&](const QString& label, int pos, const char* suffix = nullptr) {
        if (pos <= 0) return;
        if (suffix)
            out << QStringLiteral("\n  -- %1 (file %2, line %3)  [%4] --\n")
                   .arg(label).arg(side).arg(pos).arg(QLatin1String(suffix));
        else
            out << QStringLiteral("\n  -- %1 (file %2, line %3) --\n")
                   .arg(label).arg(side).arg(pos);
        printFragment(out, rel, pos, blockSize, sc.sign);
    };

    const QString label = probeLabel(probe);
    section(QStringLiteral("sys diff "), sc.diffPos);
    section(QStringLiteral("human    "), humanPos);
    section(QStringLiteral("diffmerge"), dmPos,
            label.isEmpty() ? nullptr : label.toUtf8().constData());
}

// Log files cover dm_error values kLogMin..kLogMax.
// logs[dmError - kLogMin] receives verbose detail for that shift.
// Null pointer means that shift is not logged.
static constexpr int kLogMin   = -1;
static constexpr int kLogMax   =  3;
static constexpr int kLogCount =  5;  // kLogMax - kLogMin + 1

static void evaluateRepo(const QString& csvPath,
                         const QString& repoDir,
                         int maxSlide,
                         int showErrorsMin,
                         bool useHeuristics,
                         QTextStream* logs[kLogCount],
                         EvalStats& stats,
                         QTextStream& out) {
    const QVector<CsvEntry> entries = loadCsvFile(csvPath);
    if (entries.isEmpty()) return;

    diffcore::DiffEngine engine;
    int repoTotal = 0, repoDmWrong = 0, repoGnuWrong = 0;

    for (const CsvEntry& entry : entries) {
        const QString pathA = findPairFile(repoDir, entry.digest, 0);
        const QString pathB = findPairFile(repoDir, entry.digest, 1);
        if (pathA.isEmpty() || pathB.isEmpty()) continue;

        const QStringList linesA = loadLines(pathA);
        const QStringList linesB = loadLines(pathB);
        if (linesA.isEmpty() && linesB.isEmpty()) continue;

        // Always compute without heuristics first (baseline).
        diffcore::DiffOptions baseOpts;
        baseOpts.applySliderHeuristics = false;
        const diffcore::DiffResult baseDiff = engine.compute(linesA, linesB, baseOpts);

        for (const SliderCase& sc : entry.sliders) {
            const int humanPos   = sc.blockBegin + sc.delta;
            const int baseDmPos  = findDiffmergePos(baseDiff, sc, maxSlide);

            ++stats.total;
            ++repoTotal;

            const int gnuError = -sc.delta;
            if (gnuError != 0) { ++stats.gnuWrong; ++repoGnuWrong; }

            if (baseDmPos < 0) {
                ++stats.notFound;
                continue;
            }

            // Find the matching hunk in baseDiff to get block size and probe heuristic.
            const QStringList& rel = (sc.sign == '+') ? linesB : linesA;
            int blockSize = 1;
            diffcore::HeuristicProbe probe{baseDmPos - 1, diffcore::HeuristicId::None,
                                           baseDmPos - 1, false};
            for (const diffcore::Hunk& h : baseDiff.hunks) {
                int p0 = -1, sz = 0;
                if (sc.sign == '+' && h.type == diffcore::ChangeType::Insert) {
                    p0 = h.rightRange.start; sz = h.rightRange.count;
                } else if (sc.sign == '-' && h.type == diffcore::ChangeType::Delete) {
                    p0 = h.leftRange.start; sz = h.leftRange.count;
                }
                if (p0 >= 0 && p0 + 1 == baseDmPos) {
                    blockSize = sz;
                    probe = diffcore::probeHeuristic(p0, sz, rel);
                    break;
                }
            }

            const int heuristicDmPos = probe.pos + 1;
            const int dmPos = useHeuristics ? heuristicDmPos : baseDmPos;

            // Per-heuristic stats — exclusive and H1 tracked independently.
            auto trackH = [&](int idx, int adjustedPos) {
                ++stats.hStats[idx].fired;
                const int baseErr = std::abs(baseDmPos   - humanPos);
                const int hErr    = std::abs(adjustedPos - humanPos);
                if      (hErr < baseErr) ++stats.hStats[idx].better;
                else if (hErr > baseErr) ++stats.hStats[idx].worse;
                else                     ++stats.hStats[idx].tie;
            };
            if (probe.exclusive != diffcore::HeuristicId::None)
                trackH(static_cast<int>(probe.exclusive), probe.posAfterExcl + 1);
            if (probe.h1Applied)
                trackH(static_cast<int>(diffcore::HeuristicId::H1), heuristicDmPos);

            const int dmError = dmPos - humanPos;
            ++stats.errorHist[dmError];
            if (dmError != 0) { ++stats.dmWrong; ++repoDmWrong; }

            const int absDm  = std::abs(dmError);
            const int absGnu = std::abs(gnuError);
            if      (absDm < absGnu) ++stats.dmBetter;
            else if (absDm > absGnu) ++stats.dmWorse;
            else                     ++stats.dmTie;

            if (sc.diffPos > 0) {
                ++stats.sysTotal;
                const int sysError = sc.diffPos - humanPos;
                if (sysError != 0) ++stats.sysWrong;
                const int absSys = std::abs(sysError);
                if      (absDm < absSys) ++stats.dmBetterThanSys;
                else if (absDm > absSys) ++stats.dmWorseThanSys;
                else                     ++stats.dmTieWithSys;
            }

            const bool wantShow = showErrorsMin > 0 && absDm >= showErrorsMin;
            const bool wantLog  = dmError >= kLogMin && dmError <= kLogMax
                                  && logs[dmError - kLogMin];
            if (wantShow)
                printSliderDetail(out, sc, entry.digest, humanPos, dmPos,
                                  dmError, blockSize, linesA, linesB, probe);
            if (wantLog)
                printSliderDetail(*logs[dmError - kLogMin], sc, entry.digest,
                                  humanPos, dmPos, dmError, blockSize,
                                  linesA, linesB, probe);
        }
    }

    const QFileInfo fi(csvPath);
    out << QStringLiteral("  %1  sliders: %2  gnu_wrong: %3 (%4%)  dm_wrong: %5 (%6%)\n")
           .arg(fi.baseName(), -30)
           .arg(repoTotal)
           .arg(repoGnuWrong)
           .arg(repoTotal ? 100.0 * repoGnuWrong / repoTotal : 0.0, 0, 'f', 1)
           .arg(repoDmWrong)
           .arg(repoTotal ? 100.0 * repoDmWrong / repoTotal : 0.0, 0, 'f', 1);
    out.flush();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("slider-eval"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Evaluate diffmerge's slider placement against the human corpus."));
    parser.addHelpOption();

    QCommandLineOption corpusOpt(
        QStringLiteral("corpus"),
        QStringLiteral("Path to corpusDiff directory (output of corpus-dl)."),
        QStringLiteral("path"), QStringLiteral("corpusDiff"));
    QCommandLineOption maxSlideOpt(
        QStringLiteral("max-slide"),
        QStringLiteral("Ignore a hunk match if it is more than N lines from "
                       "blockBegin. Default: 10."),
        QStringLiteral("N"), QStringLiteral("10"));
    QCommandLineOption reposOpt(
        QStringLiteral("repos"),
        QStringLiteral("Process at most N repositories (0 = all)."),
        QStringLiteral("N"), QStringLiteral("0"));
    QCommandLineOption startOpt(
        QStringLiteral("start"),
        QStringLiteral("Skip the first N repositories. Default: 0."),
        QStringLiteral("N"), QStringLiteral("0"));
    QCommandLineOption showErrorsOpt(
        QStringLiteral("show-errors"),
        QStringLiteral("Print file context for every slider where |dm_error| >= N. "
                       "Shows the block in the file at sys-diff pos and at human pos "
                       "so you can see visually what each algorithm chose."),
        QStringLiteral("N"));
    QCommandLineOption logsOpt(
        QStringLiteral("logs"),
        QStringLiteral("Write verbose slider details into DIR/log-1, DIR/log0 … DIR/log3, "
                       "one file per shift value (dm_error = -1..3)."),
        QStringLiteral("DIR"));
    QCommandLineOption heuristicsOpt(
        QStringLiteral("heuristics"),
        QStringLiteral("Apply slider-placement heuristics before measuring dm_error. "
                       "(1) If block starts with an empty line and the line after the "
                       "block is also empty, slide forward by 1."));
    parser.addOption(corpusOpt);
    parser.addOption(maxSlideOpt);
    parser.addOption(reposOpt);
    parser.addOption(startOpt);
    parser.addOption(showErrorsOpt);
    parser.addOption(logsOpt);
    parser.addOption(heuristicsOpt);
    parser.process(app);

    const QString corpusDir     = parser.value(corpusOpt);
    const int     maxSlide      = parser.value(maxSlideOpt).toInt();
    const int     repoLimit     = parser.value(reposOpt).toInt();
    const int     startOffset   = parser.value(startOpt).toInt();
    const int     showErrorsMin = parser.isSet(showErrorsOpt)
                                  ? parser.value(showErrorsOpt).toInt() : 0;
    const QString logsDir       = parser.value(logsOpt);
    const bool    useHeuristics = parser.isSet(heuristicsOpt);

    // Open per-shift log files if --logs was given.
    QFile        logFiles[kLogCount];
    QTextStream* logs[kLogCount] = {};
    if (!logsDir.isEmpty()) {
        QDir().mkpath(logsDir);
        for (int k = kLogMin; k <= kLogMax; ++k) {
            const int idx = k - kLogMin;
            logFiles[idx].setFileName(logsDir + QStringLiteral("/log") + QString::number(k));
            if (logFiles[idx].open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
                logs[idx] = new QTextStream(&logFiles[idx]);
            else
                QTextStream(stderr) << "cannot open: " << logFiles[idx].fileName() << '\n';
        }
    }

    QTextStream out(stdout);
    out << "Corpus   : " << corpusDir << "\n";
    out << "max-slide: " << maxSlide << " lines\n";
    if (showErrorsMin > 0)
        out << "show-errors: |dm_error| >= " << showErrorsMin << "\n";
    if (!logsDir.isEmpty())
        out << "logs dir : " << logsDir << "/log-1 .. log3\n";
    if (useHeuristics)
        out << "heuristics: ON\n";
    out << "\n";

    QDir dir(corpusDir);
    if (!dir.exists()) {
        QTextStream(stderr) << "directory not found: " << corpusDir << '\n';
        return 2;
    }

    QStringList csvFiles = dir.entryList({QStringLiteral("*.csv")},
                                          QDir::Files, QDir::Name);
    const int totalFiles = csvFiles.size();
    if (startOffset > 0)
        csvFiles = csvFiles.mid(startOffset);
    if (repoLimit > 0 && csvFiles.size() > repoLimit)
        csvFiles = csvFiles.mid(0, repoLimit);

    out << "Repositories to process: " << csvFiles.size()
        << " (of " << totalFiles << " total";
    if (startOffset > 0) out << ", starting at #" << startOffset + 1;
    out << ")\n\n";

    EvalStats stats;
    int i = 0;
    for (const QString& csvName : csvFiles) {
        ++i;
        const QString csvPath = dir.filePath(csvName);
        const QString repoDir = dir.filePath(QFileInfo(csvName).baseName());
        out << '[' << i << '/' << csvFiles.size() << "] " << csvName << '\n';
        out.flush();
        evaluateRepo(csvPath, repoDir, maxSlide, showErrorsMin, useHeuristics,
                     logs, stats, out);
    }

    out << "\n=== SUMMARY ===\n";
    out << formatStats(stats);

    for (int k = 0; k < kLogCount; ++k) delete logs[k];
    return 0;
}
