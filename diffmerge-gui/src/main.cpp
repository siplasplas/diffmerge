#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include "MainWindow.h"
#include "LaunchOptions.h"
int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    QApplication::setApplicationName(QStringLiteral("DiffMerge"));
    QApplication::setOrganizationName(QStringLiteral("DiffMerge"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Compare two files or directories"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("paths"),QStringLiteral("Zero, one (prefill), or two existing files/directories"),QStringLiteral("[LEFT [RIGHT]]"));
    parser.addOption({{QStringLiteral("L"),QStringLiteral("label")},QStringLiteral("Display label and fallback syntax file name (repeat for the right side)"),QStringLiteral("LABEL")});
    parser.addOption({QStringLiteral("edit"), QStringLiteral("Editable sides: right (default), left, both, none"), QStringLiteral("SIDE"), QStringLiteral("right")});
    parser.addOption({QStringLiteral("readonly"), QStringLiteral("Make both sides read-only")});
    parser.addOption({{QStringLiteral("w"),QStringLiteral("ignore-whitespace")},QStringLiteral("Ignore leading/trailing whitespace and collapse internal whitespace runs")});
    parser.addOption({{QStringLiteral("b"),QStringLiteral("ignore-trailing-whitespace")},QStringLiteral("Ignore trailing whitespace")});
    parser.addOption({{QStringLiteral("i"),QStringLiteral("ignore-case")},QStringLiteral("Ignore case")});
    if(!parser.parse(app.arguments())) {
        QTextStream(stderr)<<parser.errorText()<<'\n'; return 2;
    }
    if(parser.isSet(QStringLiteral("help"))) parser.showHelp();
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
