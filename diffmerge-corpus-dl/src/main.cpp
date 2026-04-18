#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

#include "CorpusProcessor.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("corpus-dl"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Download slider corpus file pairs from GitHub raw content."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption corpusOpt(
        QStringLiteral("corpus"),
        QStringLiteral("Path to diff-slider-tools/corpus directory."),
        QStringLiteral("path"),
        QStringLiteral("/home/andrzej/git/diff-slider-tools/corpus"));
    QCommandLineOption outputOpt(
        QStringLiteral("output"),
        QStringLiteral("Output directory for downloaded files."),
        QStringLiteral("path"),
        QStringLiteral("corpusDiff"));
    parser.addOption(corpusOpt);
    parser.addOption(outputOpt);
    parser.process(app);

    const QString corpusDir = parser.value(corpusOpt);
    const QString outputDir = parser.value(outputOpt);

    QTextStream(stdout) << "corpus : " << corpusDir << '\n'
                        << "output : " << outputDir << '\n';

    diffmerge::corpus::CorpusProcessor proc(corpusDir, outputDir);
    proc.run();

    return 0;
}
