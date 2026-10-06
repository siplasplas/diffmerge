#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include "MainWindow.h"
#include "LaunchOptions.h"
#include "MergeDialog.h"
int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    QApplication::setApplicationName(QStringLiteral("DiffMerge"));
    QApplication::setOrganizationName(QStringLiteral("DiffMerge"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Compare files/directories or resolve an existing merge RESULT"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("paths"),QStringLiteral("Comparison: zero, one, or two paths. Merge: BASE LOCAL REMOTE with -o MERGED"),QStringLiteral("[PATHS...]"));
    parser.addOption({QStringLiteral("merge"),QStringLiteral("Open BASE LOCAL REMOTE as immutable sources and preserve the existing -o MERGED result")});
    parser.addOption({{QStringLiteral("o"),QStringLiteral("output")},QStringLiteral("Existing merge RESULT file (required with --merge)"),QStringLiteral("MERGED")});
    parser.addOption({QStringLiteral("base-absent"),QStringLiteral("Explicitly absent BASE; supply '-' as the BASE argument")});
    parser.addOption({QStringLiteral("marker-size"),QStringLiteral("Conflict marker run length for --merge"),QStringLiteral("N"),QStringLiteral("7")});
    parser.addOption({{QStringLiteral("L"),QStringLiteral("label")},QStringLiteral("Display labels: LEFT/RIGHT for compare, BASE/LOCAL/REMOTE/RESULT for merge (repeat)"),QStringLiteral("LABEL")});
    parser.addOption({QStringLiteral("edit"), QStringLiteral("Editable sides: right (default), left, both, none"), QStringLiteral("SIDE"), QStringLiteral("right")});
    parser.addOption({QStringLiteral("readonly"), QStringLiteral("Read-only comparison or merge inspection (merge exits nonzero)")});
    parser.addOption({{QStringLiteral("w"),QStringLiteral("ignore-whitespace")},QStringLiteral("Ignore leading/trailing whitespace and collapse internal whitespace runs")});
    parser.addOption({{QStringLiteral("b"),QStringLiteral("ignore-trailing-whitespace")},QStringLiteral("Ignore trailing whitespace")});
    parser.addOption({{QStringLiteral("i"),QStringLiteral("ignore-case")},QStringLiteral("Ignore case")});
    if(!parser.parse(app.arguments())) {
        QTextStream(stderr)<<parser.errorText()<<'\n'; return 2;
    }
    if(parser.isSet(QStringLiteral("help"))) parser.showHelp();
    if (parser.isSet(QStringLiteral("merge"))) {
        using namespace diffmerge::gui;
        const auto paths=parser.positionalArguments();
        if (paths.size()!=3) { QTextStream(stderr)<<"--merge requires BASE LOCAL REMOTE and -o MERGED\n"; return 2; }
        if (parser.isSet(QStringLiteral("edit")) || parser.isSet("w") || parser.isSet("b") || parser.isSet("i")) {
            QTextStream(stderr)<<"--edit and comparison ignore options are not supported in merge mode; use --readonly for inspection\n"; return 2;
        }
        MergeLaunchOptions merge;
        merge.basePath=paths[0]; merge.localPath=paths[1]; merge.remotePath=paths[2]; merge.resultPath=parser.value(QStringLiteral("output"));
        merge.labels=parser.values(QStringLiteral("label")); merge.readOnly=parser.isSet(QStringLiteral("readonly"));
        merge.baseAbsent=parser.isSet(QStringLiteral("base-absent"));
        bool markerValid=false; merge.markerSize=parser.value(QStringLiteral("marker-size")).toInt(&markerValid);
        const auto error=markerValid ? validateMergeLaunch(merge) : QStringLiteral("--marker-size must be an integer");
        if (!error.isEmpty()) { QTextStream(stderr)<<error<<'\n'; return 2; }
        return desktop::runMergeDialog(merge);
    }
    if (parser.isSet(QStringLiteral("output")) || parser.isSet(QStringLiteral("base-absent")) || parser.isSet(QStringLiteral("marker-size"))) {
        QTextStream(stderr)<<"--output, --base-absent and --marker-size require --merge\n"; return 2;
    }
    const auto options=diffmerge::gui::validateLaunchPaths(parser.positionalArguments(),parser.values(QStringLiteral("label")));
    if(options.kind==diffmerge::gui::LaunchKind::Error) {
        QTextStream(stderr)<<options.error<<'\n';
        QMessageBox::critical(nullptr,QStringLiteral("Cannot compare paths"),options.error); return 2;
    }
    diffmerge::gui::MainWindow window;
    const auto edit = parser.isSet(QStringLiteral("readonly")) ? QStringLiteral("none") : parser.value(QStringLiteral("edit"));
    if (edit != "right" && edit != "left" && edit != "both" && edit != "none") {
        QTextStream(stderr) << "--edit must be right, left, both or none\n"; return 2;
    }
    window.setEditMode(edit == "left" || edit == "both", edit == "right" || edit == "both");
    if(parser.isSet("w") || parser.isSet("b") || parser.isSet("i"))
        window.setIgnoreOptions(parser.isSet("w"),parser.isSet("b"),parser.isSet("i"));
    using diffmerge::gui::LaunchKind;
    switch(options.kind) {
        case LaunchKind::PrefillFile: window.prefillFiles(options.paths[0]); break;
        case LaunchKind::PrefillDirectory: window.prefillDirs(options.paths[0]); break;
        case LaunchKind::Files: window.loadFiles(options.paths[0],options.paths[1]); break;
        case LaunchKind::Directories: window.loadDirectories(options.paths[0],options.paths[1]); break;
        default: break;
    }
    window.setFileLabels(options.labels);
    window.show();
    if(options.kind==LaunchKind::Empty) QTimer::singleShot(0,&window,&diffmerge::gui::MainWindow::chooseFiles);
    return QApplication::exec();
}
