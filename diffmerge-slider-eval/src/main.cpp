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

// Load a text file as a QStringList of lines.
static QStringList loadLines(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QStringList lines;
    while (!in.atEnd())
        lines.append(in.readLine());
    return lines;
}

// Find a file matching "<repoDir>/<digest>_<side>.*" (any extension).
static QString findPairFile(const QString& repoDir,
                            const QString& digest, int side) {
    const QString prefix = digest + QLatin1Char('_') + QString::number(side);
    const QStringList candidates =
        QDir(repoDir).entryList({prefix + QStringLiteral(".*")}, QDir::Files);
    if (candidates.isEmpty()) return {};
    return repoDir + QLatin1Char('/') + candidates.first();
}

// Evaluate one CSV file (= one repo) and accumulate results into stats.
static void evaluateRepo(const QString& csvPath,
                         const QString& repoDir,
                         int maxSlide,
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

            // gnu_error = blockBegin - humanPos = -delta
            const int gnuError = -sc.delta;
            if (gnuError != 0) { ++stats.gnuWrong; ++repoGnuWrong; }

            if (dmPos < 0) {
                ++stats.notFound;
                continue;   // skip from dm stats — hunk outside maxSlide window
            }

            const int dmError = dmPos - humanPos;
            ++stats.errorHist[dmError];
            if (dmError != 0) { ++stats.dmWrong; ++repoDmWrong; }

            // Better / worse / tie vs git diff
            const int absDm  = std::abs(dmError);
            const int absGnu = std::abs(gnuError);
            if      (absDm < absGnu) ++stats.dmBetter;
            else if (absDm > absGnu) ++stats.dmWorse;
            else                     ++stats.dmTie;
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
        QStringLiteral("path"),
        QStringLiteral("corpusDiff"));
    QCommandLineOption maxSlideOpt(
        QStringLiteral("max-slide"),
        QStringLiteral("Ignore a hunk match if it is more than N lines from "
                       "blockBegin (the git diff position). Prevents false matches "
                       "with unrelated changes in the same file. Default: 10."),
        QStringLiteral("N"),
        QStringLiteral("10"));
    QCommandLineOption reposOpt(
        QStringLiteral("repos"),
        QStringLiteral("Process at most N repositories (0 = all). "
                       "Each repo is one CSV file, e.g. cpython&cpython.csv."),
        QStringLiteral("N"),
        QStringLiteral("0"));
    parser.addOption(corpusOpt);
    parser.addOption(maxSlideOpt);
    parser.addOption(reposOpt);
    parser.process(app);

    const QString corpusDir = parser.value(corpusOpt);
    const int     maxSlide  = parser.value(maxSlideOpt).toInt();
    const int     repoLimit = parser.value(reposOpt).toInt();

    QTextStream out(stdout);
    out << "Corpus   : " << corpusDir << "\n";
    out << "max-slide: " << maxSlide << " lines\n\n";

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
        const QString csvPath  = dir.filePath(csvName);
        const QString repoDir  = dir.filePath(QFileInfo(csvName).baseName());
        out << '[' << i << '/' << csvFiles.size() << "] " << csvName << '\n';
        out.flush();
        evaluateRepo(csvPath, repoDir, maxSlide, stats, out);
    }

    out << "\n=== SUMMARY ===\n";
    out << formatStats(stats);
    return 0;
}
