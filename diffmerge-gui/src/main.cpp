#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <qce/kate/KatePaths.h>
#include <qxmigration.h>
#include "MainWindow.h"
#include "LaunchOptions.h"
#include "MergeDialog.h"
#include "ConflictSidesDialog.h"
#include "ResolveDialog.h"
namespace {
// Settings of older versions, in ~/.config/DiffMerge/DiffMerge.conf: only the keys below.
bool isDiffMergeSettings(const QString& path) {
    const QSettings settings(path, QSettings::IniFormat);
    for (const QString& key : settings.allKeys()) {
        if (!key.startsWith(QLatin1String("view/")) && !key.startsWith(QLatin1String("comparison/")) &&
            !key.startsWith(QLatin1String("directories/")) && key != QLatin1String("syntaxDownloadDeclined"))
            return false;
    }
    return true;
}

// Settings to ~/.config/siplasplas/diffmerge.conf and the Kate syntax data to the shared
// ~/.local/share/kate-data, once; what was moved goes to stderr.
void migrateFiles() {
    const QString oldDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/DiffMerge");
    QxMigrationSpec spec;
    spec.oldSettingsFiles = {oldDir + QStringLiteral("/DiffMerge.conf")};
    spec.oldDirectories = {oldDir};
    spec.recognize = isDiffMergeSettings;
    const QxMigrationResult moved = qxMigrateToSiplasplas(spec);
    for (const QString& line : moved.log) QTextStream(stderr) << "Moving DiffMerge's files: " << line << '\n';
    if (moved.status == QxMigrationResult::Failed) {
        // The old settings file for this run; the next start tries again.
        QApplication::setOrganizationName(QStringLiteral("DiffMerge"));
        QApplication::setApplicationName(QStringLiteral("DiffMerge"));
    }
    const qce::kate::DataRootMigration kate = qce::kate::migrateDataRoot();
    for (const QString& line : kate.log) QTextStream(stderr) << "Moving Kate data: " << line << '\n';
    for (const QString& line : kate.warnings) QTextStream(stderr) << "Moving Kate data: " << line << '\n';
}
} // namespace

int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    // Settings in ~/.config/siplasplas/diffmerge.conf, data in ~/.local/share/siplasplas/diffmerge.
    QApplication::setOrganizationName(QStringLiteral("siplasplas"));
    QApplication::setApplicationName(QStringLiteral("diffmerge"));
    QApplication::setApplicationDisplayName(QStringLiteral("DiffMerge"));
    migrateFiles();
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Compare files/directories or resolve an existing merge RESULT"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("paths"),QStringLiteral("Comparison: zero, one, or two paths. Merge: BASE LOCAL REMOTE with -o MERGED. Conflict sides: one marker file"),QStringLiteral("[PATHS...]"));
    parser.addOption({QStringLiteral("merge"),QStringLiteral("Open BASE LOCAL REMOTE as immutable sources and preserve the existing -o MERGED result")});
    parser.addOption({QStringLiteral("resolve-conflicts"),QStringLiteral("Automatically resolve and review one marker-based conflict file")});
    parser.addOption({QStringLiteral("conflict-sides"),QStringLiteral("Compare both sides of one marker-based conflict file in read-only panes")});
    parser.addOption({{QStringLiteral("o"),QStringLiteral("output")},QStringLiteral("Existing merge RESULT file (required with --merge)"),QStringLiteral("MERGED")});
    parser.addOption({QStringLiteral("base-absent"),QStringLiteral("Explicitly absent BASE; supply '-' as the BASE argument")});
    parser.addOption({QStringLiteral("marker-size"),QStringLiteral("Conflict marker run length for --merge, --conflict-sides or --resolve-conflicts"),QStringLiteral("N"),QStringLiteral("7")});
    parser.addOption({{QStringLiteral("L"),QStringLiteral("label")},QStringLiteral("Display labels: LEFT/RIGHT for compare, BASE/LOCAL/REMOTE/RESULT for merge (repeat)"),QStringLiteral("LABEL")});
    parser.addOption({QStringLiteral("edit"), QStringLiteral("Editable sides: right, left, both, none (default: each side "
                                                             "unless its file is not writable)"), QStringLiteral("SIDE"), QStringLiteral("right")});
    parser.addOption({QStringLiteral("readonly"), QStringLiteral("Read-only comparison or merge inspection (merge exits nonzero)")});
    parser.addOption({{QStringLiteral("w"),QStringLiteral("ignore-whitespace")},QStringLiteral("Ignore leading/trailing whitespace and collapse internal whitespace runs")});
    parser.addOption({{QStringLiteral("b"),QStringLiteral("ignore-trailing-whitespace")},QStringLiteral("Ignore trailing whitespace")});
    parser.addOption({{QStringLiteral("i"),QStringLiteral("ignore-case")},QStringLiteral("Ignore case")});
    if(!parser.parse(app.arguments())) {
        QTextStream(stderr)<<parser.errorText()<<'\n'; return 2;
    }
    if(parser.isSet(QStringLiteral("help"))) parser.showHelp();
    if (parser.isSet(QStringLiteral("resolve-conflicts"))) {
        const auto paths=parser.positionalArguments();
        if(paths.size()!=1 || parser.isSet("merge") || parser.isSet("conflict-sides") || parser.isSet("output") ||
           parser.isSet("base-absent") || parser.isSet("label") || parser.isSet("edit") || parser.isSet("readonly") || parser.isSet("w") || parser.isSet("b") || parser.isSet("i")) {
            QTextStream(stderr)<<"--resolve-conflicts requires one marker file and cannot be combined with comparison/merge options\n"; return 2;
        }
        bool valid=false; const int size=parser.value("marker-size").toInt(&valid);
        if(!valid || size<1 || size>200000) { QTextStream(stderr)<<"Invalid marker size\n"; return 2; }
        return diffmerge::gui::desktop::runResolveDialog(paths.first(),size);
    }
    if (parser.isSet(QStringLiteral("conflict-sides"))) {
        const auto paths=parser.positionalArguments();
        if (paths.size()!=1 || parser.isSet(QStringLiteral("merge")) || parser.isSet(QStringLiteral("output")) ||
            parser.isSet(QStringLiteral("base-absent")) || parser.isSet(QStringLiteral("edit")) ||
            parser.isSet(QStringLiteral("label")) || parser.isSet("w") || parser.isSet("b") || parser.isSet("i")) {
            QTextStream(stderr)<<"--conflict-sides requires one conflict file and cannot be combined with merge or comparison options\n"; return 2;
        }
        bool markerValid=false;
        const int markerSize=parser.value(QStringLiteral("marker-size")).toInt(&markerValid);
        if (!markerValid || markerSize<1 || markerSize>200000) {
            QTextStream(stderr)<<"--marker-size must be between 1 and 200000\n"; return 2;
        }
        return diffmerge::gui::desktop::runConflictSidesDialog(paths.first(),markerSize);
    }
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
    // No --edit nor --readonly: the automatic mode, by the paths (MainWindow::autoEditable)
    if (parser.isSet(QStringLiteral("edit")) || parser.isSet(QStringLiteral("readonly"))) {
        window.setAutoEdit(false);
        window.setEditMode(edit == "left" || edit == "both", edit == "right" || edit == "both");
    }
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
