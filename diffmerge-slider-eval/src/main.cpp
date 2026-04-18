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

// Print N lines of context around 1-based position pos in lines, with a marker.
static void printContext(QTextStream& out,
                         const QStringList& lines,
                         int pos, int ctx = 3) {
    const int from = qMax(1, pos - ctx);
    const int to   = qMin(lines.size(), pos + ctx);
    for (int i = from; i <= to; ++i) {
        const QString marker = (i == pos) ? QStringLiteral(">>>") : QStringLiteral("   ");
        out << QStringLiteral("    %1 %2: %3\n")
               .arg(marker).arg(i, 4).arg(lines[i - 1]);
    }
}

static void printSliderDetail(QTextStream& out,
                              const SliderCase& sc,
                              const QString& digest,
                              int humanPos, int dmPos, int dmError,
                              const QStringList& linesA,
                              const QStringList& linesB) {
    out << QStringLiteral("\n--- SLIDER %1 %2 %3 diffPos=%4  digest=%5\n")
           .arg(QChar(sc.sign)).arg(sc.blockBegin).arg(sc.delta).arg(sc.diffPos)
           .arg(digest);
    out << QStringLiteral("    git diff pos : %1\n").arg(sc.blockBegin);
    out << QStringLiteral("    sys diff pos : %1\n").arg(sc.diffPos);
    out << QStringLiteral("    human pos    : %1  (delta=%2)\n").arg(humanPos).arg(sc.delta);
    out << QStringLiteral("    diffmerge pos: %1  (dm_error=%2)\n").arg(dmPos).arg(dmError);

    // For '+' sliders the relevant file is B (right); for '-' it is A (left).
    const QStringList& relevant = (sc.sign == '+') ? linesB : linesA;
    const char side = (sc.sign == '+') ? 'B' : 'A';

    if (sc.diffPos > 0 && sc.diffPos != humanPos) {
        out << QStringLiteral("  context at sys/dm pos (file %1, line %2):\n")
               .arg(side).arg(sc.diffPos);
        printContext(out, relevant, sc.diffPos);
        out << QStringLiteral("  context at human pos  (file %1, line %2):\n")
               .arg(side).arg(humanPos);
        printContext(out, relevant, humanPos);
    } else {
        out << QStringLiteral("  context at diffmerge pos (file %1, line %2):\n")
               .arg(side).arg(dmPos);
        printContext(out, relevant, dmPos);
    }
}

static void evaluateRepo(const QString& csvPath,
                         const QString& repoDir,
                         int maxSlide,
                         int showErrorsMin,
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
            const int dmPos    = findDiffmergePos(diff, sc, maxSlide);

            ++stats.total;
            ++repoTotal;

            const int gnuError = -sc.delta;
            if (gnuError != 0) { ++stats.gnuWrong; ++repoGnuWrong; }

            if (dmPos < 0) {
                ++stats.notFound;
                continue;
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

            if (showErrorsMin > 0 && absDm >= showErrorsMin) {
                printSliderDetail(out, sc, entry.digest,
                                  humanPos, dmPos, dmError,
                                  linesA, linesB);
            }
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
    parser.addOption(corpusOpt);
    parser.addOption(maxSlideOpt);
    parser.addOption(reposOpt);
    parser.addOption(showErrorsOpt);
    parser.process(app);

    const QString corpusDir     = parser.value(corpusOpt);
    const int     maxSlide      = parser.value(maxSlideOpt).toInt();
    const int     repoLimit     = parser.value(reposOpt).toInt();
    const int     showErrorsMin = parser.isSet(showErrorsOpt)
                                  ? parser.value(showErrorsOpt).toInt() : 0;

    QTextStream out(stdout);
    out << "Corpus   : " << corpusDir << "\n";
    out << "max-slide: " << maxSlide << " lines\n";
    if (showErrorsMin > 0)
        out << "show-errors: |dm_error| >= " << showErrorsMin << "\n";
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
        evaluateRepo(csvPath, repoDir, maxSlide, showErrorsMin, stats, out);
    }

    out << "\n=== SUMMARY ===\n";
    out << formatStats(stats);
    return 0;
}
