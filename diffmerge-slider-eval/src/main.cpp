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

// Leading indentation width in columns. Tab counts as 4 spaces.
static int indentWidth(const QString& line) {
    int w = 0;
    for (QChar c : line) {
        if (c == QLatin1Char('\t'))      w += 4;
        else if (c == QLatin1Char(' '))  ++w;
        else break;
    }
    return w;
}

// Indexing (0-based into rel):
//   rel[p]              first line of block
//   rel[p + size - 1]   last line of block
//   rel[p - 1]          line before block
//   rel[p + size]       line after block
//
// H2 (tried first): find the largest k such that the k lines before the
// block match the k last lines of the block, i.e.
//     rel[p - i] == rel[p + size - i]   for i = 1..k
// If k > 0 and indent(rel[p-k]) < indent(rel[p]), slide back by k (keeping
// the less-indented boundary outside the block).
//
// H1 (tried only when H2 didn't fire): if the block starts with n empty
// lines and is followed by m empty lines, slide forward by min(n, m) so the
// empty lines end up after the block.
static int applyHeuristics(int dmPos, int blockSize,
                            const QStringList& rel) {
    if (dmPos <= 0 || dmPos > rel.size() || blockSize < 1) return dmPos;
    const int p    = dmPos - 1;
    const int size = blockSize;
    const int N    = rel.size();

    // H2: max matching run of NON-EMPTY boundary lines (empty runs are left
    // for H1 to handle, so the two heuristics don't overlap).
    int k2 = 0;
    while (p - (k2 + 1) >= 0 && p + size - (k2 + 1) < N &&
           rel[p - (k2 + 1)] == rel[p + size - (k2 + 1)] &&
           !rel[p - (k2 + 1)].trimmed().isEmpty())
        ++k2;
    if (k2 > 0) {
        const int indLeft  = indentWidth(rel[p - k2]);
        const int indFirst = indentWidth(rel[p]);
        bool shift = (indLeft < indFirst);
        // Equal indent also allowed when all k absorbed lines start with "//"
        // (block starts with a block of line comments).
        if (!shift && indLeft == indFirst) {
            bool allComments = true;
            for (int i = 1; i <= k2; ++i) {
                if (!rel[p - i].trimmed().startsWith(QStringLiteral("//"))) {
                    allComments = false;
                    break;
                }
            }
            shift = allComments;
        }
        if (shift) return (p - k2) + 1;
    }

    // H1: empty-line boundary runs.
    int n = 0;
    while (n < size && p + n < N && rel[p + n].trimmed().isEmpty())
        ++n;
    int m = 0;
    while (p + size + m < N && rel[p + size + m].trimmed().isEmpty())
        ++m;
    const int k1 = std::min(n, m);
    if (k1 > 0) return (p + k1) + 1;

    // H3: line before block starts with "/*" and equals block's last line
    // (single-line slide-left valid) — slide back by 1.
    if (p > 0 && p + size - 1 < N &&
        rel[p - 1] == rel[p + size - 1] &&
        rel[p - 1].trimmed().startsWith(QStringLiteral("/*"))) {
        return (p - 1) + 1;
    }

    return dmPos;
}

// Find the size (line count) of the hunk that starts at 1-based dmPos.
// Returns 1 if no matching hunk is found.
static int findHunkSize(const diffcore::DiffResult& result,
                        const SliderCase& sc, int dmPos) {
    for (const diffcore::Hunk& h : result.hunks) {
        if (sc.sign == '+' && h.type != diffcore::ChangeType::Insert) continue;
        if (sc.sign == '-' && h.type != diffcore::ChangeType::Delete) continue;
        const int start1 = ((sc.sign == '+') ? h.rightRange.start
                                              : h.leftRange.start) + 1;
        if (start1 == dmPos)
            return (sc.sign == '+') ? h.rightRange.count : h.leftRange.count;
    }
    return 1;
}

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

static void printSliderDetail(QTextStream& out,
                              const SliderCase& sc,
                              const QString& digest,
                              int humanPos, int dmPos, int dmError,
                              int blockSize,
                              const QStringList& linesA,
                              const QStringList& linesB) {
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

    auto section = [&](const QString& label, int pos) {
        if (pos <= 0) return;
        out << QStringLiteral("\n  -- %1 (file %2, line %3) --\n")
               .arg(label).arg(side).arg(pos);
        printFragment(out, rel, pos, blockSize, sc.sign);
    };

    section(QStringLiteral("sys diff "), sc.diffPos);
    section(QStringLiteral("human    "), humanPos);
    section(QStringLiteral("diffmerge"), dmPos);
}

// logs[k] receives verbose detail for sliders where dm_error == k (k = 0..3).
// Null pointer means that shift is not logged.
static void evaluateRepo(const QString& csvPath,
                         const QString& repoDir,
                         int maxSlide,
                         int showErrorsMin,
                         bool useHeuristics,
                         QTextStream* logs[4],
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

        const diffcore::DiffResult diff = engine.compute(linesA, linesB);

        for (const SliderCase& sc : entry.sliders) {
            const int humanPos = sc.blockBegin + sc.delta;
            int       dmPos    = findDiffmergePos(diff, sc, maxSlide);

            ++stats.total;
            ++repoTotal;

            const int gnuError = -sc.delta;
            if (gnuError != 0) { ++stats.gnuWrong; ++repoGnuWrong; }

            if (dmPos < 0) {
                ++stats.notFound;
                continue;
            }

            // Look up block size from the engine's hunk BEFORE any heuristic
            // shift (once shifted, dmPos no longer matches any hunk's start).
            const int blockSize = findHunkSize(diff, sc, dmPos);
            if (useHeuristics) {
                const QStringList& rel = (sc.sign == '+') ? linesB : linesA;
                dmPos = applyHeuristics(dmPos, blockSize, rel);
            }

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
            const bool wantLog  = dmError >= 0 && dmError <= 3 && logs[dmError];
            if (wantShow)
                printSliderDetail(out, sc, entry.digest, humanPos, dmPos,
                                  dmError, blockSize, linesA, linesB);
            if (wantLog)
                printSliderDetail(*logs[dmError], sc, entry.digest, humanPos,
                                  dmPos, dmError, blockSize, linesA, linesB);
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
    QCommandLineOption showErrorsOpt(
        QStringLiteral("show-errors"),
        QStringLiteral("Print file context for every slider where |dm_error| >= N. "
                       "Shows the block in the file at sys-diff pos and at human pos "
                       "so you can see visually what each algorithm chose."),
        QStringLiteral("N"));
    QCommandLineOption logsOpt(
        QStringLiteral("logs"),
        QStringLiteral("Write verbose slider details into DIR/log0 … DIR/log3, "
                       "one file per shift value (dm_error = 0..3)."),
        QStringLiteral("DIR"));
    QCommandLineOption heuristicsOpt(
        QStringLiteral("heuristics"),
        QStringLiteral("Apply slider-placement heuristics before measuring dm_error. "
                       "(1) If block starts with an empty line and the line after the "
                       "block is also empty, slide forward by 1."));
    parser.addOption(corpusOpt);
    parser.addOption(maxSlideOpt);
    parser.addOption(reposOpt);
    parser.addOption(showErrorsOpt);
    parser.addOption(logsOpt);
    parser.addOption(heuristicsOpt);
    parser.process(app);

    const QString corpusDir     = parser.value(corpusOpt);
    const int     maxSlide      = parser.value(maxSlideOpt).toInt();
    const int     repoLimit     = parser.value(reposOpt).toInt();
    const int     showErrorsMin = parser.isSet(showErrorsOpt)
                                  ? parser.value(showErrorsOpt).toInt() : 0;
    const QString logsDir       = parser.value(logsOpt);
    const bool    useHeuristics = parser.isSet(heuristicsOpt);

    // Open per-shift log files if --logs was given.
    QFile     logFiles[4];
    QTextStream* logs[4] = {nullptr, nullptr, nullptr, nullptr};
    if (!logsDir.isEmpty()) {
        QDir().mkpath(logsDir);
        for (int k = 0; k <= 3; ++k) {
            logFiles[k].setFileName(logsDir + QStringLiteral("/log") + QString::number(k));
            if (logFiles[k].open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
                logs[k] = new QTextStream(&logFiles[k]);
            else
                QTextStream(stderr) << "cannot open: " << logFiles[k].fileName() << '\n';
        }
    }

    QTextStream out(stdout);
    out << "Corpus   : " << corpusDir << "\n";
    out << "max-slide: " << maxSlide << " lines\n";
    if (showErrorsMin > 0)
        out << "show-errors: |dm_error| >= " << showErrorsMin << "\n";
    if (!logsDir.isEmpty())
        out << "logs dir : " << logsDir << "/log0 .. log3\n";
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
    if (repoLimit > 0 && csvFiles.size() > repoLimit)
        csvFiles = csvFiles.mid(0, repoLimit);

    out << "Repositories to process: " << csvFiles.size()
        << " (of " << dir.entryList({QStringLiteral("*.csv")},
                                     QDir::Files).size() << " total)\n\n";

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

    for (int k = 0; k <= 3; ++k) delete logs[k];
    return 0;
}
