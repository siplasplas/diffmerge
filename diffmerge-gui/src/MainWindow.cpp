#include "MainWindow.h"
#include "MergeResultSave.h"

#include <qxfiledialog.h>
#include <QShortcut>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QPushButton>
#include <QPromise>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <diffmerge/MergeWidget.h>
#include <QCheckBox>
#include <QInputDialog>
#include <QSlider>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <qce/CodeEditArea.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStringConverter>
#include <diffmerge/DiffEditor.h>
#include <diffmerge/DirectoryOperations.h>
#include <QActionGroup>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <qce/kate/KateDataDownloader.h>

#include <diffmerge/DirDiffWidget.h>
#include <diffmerge/FileDiffWidget.h>

namespace diffmerge::gui {
namespace {
struct MergePreviewLoadResult {
    PrepareMergeSessionResult prepared;
    std::optional<desktop::MergeSaveStamp> stamp;
};
class MergeEditDialog : public QDialog {
public:
    using QDialog::QDialog;
    MergeWidget* merge = nullptr;
    bool saving = false, closeAfterSave = false;
    diffcore::CancellationToken saveCancellation;
    std::function<bool(bool)> saveAndMaybeClose;
    void reject() override {
        if (saving) { saveCancellation.requestCancellation(); return; }
        if (merge && merge->isModified()) {
            const auto answer = QMessageBox::question(this,QStringLiteral("Unsaved merge result"),
                QStringLiteral("RESULT has unsaved changes and %1 unresolved conflicts. Save before closing?").arg(merge->unresolvedCount()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) return;
            if (answer == QMessageBox::Save) { if (saveAndMaybeClose) saveAndMaybeClose(true); return; }
        }
        QDialog::reject();
    }
};
QByteArray fileStamp(const QString& path) {
    if(path.isEmpty()) return {};
    const QFileInfo info(path);
    if(!info.exists()) return QByteArrayLiteral("missing");
    QByteArray stamp=info.canonicalFilePath().toUtf8()+':'+QByteArray::number(info.size())+':'+QByteArray::number(int(info.permissions()));
    QFile file(path);
    if(info.size()<=8*1024*1024 && file.open(QIODevice::ReadOnly)) {
        const auto bytes=file.read(8*1024*1024+1);
        if(file.error()==QFileDevice::NoError) return stamp+QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    }
    return stamp+':'+QByteArray::number(info.lastModified().toMSecsSinceEpoch());
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_stack      = new QStackedWidget(this);
    m_diffWidget = new FileDiffWidget(m_stack);
    m_fileWatcher = new QFileSystemWatcher(this);
    m_fileWatchTimer = new QTimer(this); m_fileWatchTimer->setSingleShot(true); m_fileWatchTimer->setInterval(200);
    connect(m_fileWatcher,&QFileSystemWatcher::fileChanged,this,[this] { m_fileWatchTimer->start(); });
    connect(m_fileWatcher,&QFileSystemWatcher::directoryChanged,this,[this] { m_fileWatchTimer->start(); });
    connect(m_fileWatchTimer,&QTimer::timeout,this,&MainWindow::checkFileChanges);
    connect(m_diffWidget,&FileDiffWidget::pathsChanged,this,[this] {
        configureFileWatching(true);
        // Directory activation can assign a missing side's save target after load.
        QTimer::singleShot(0,this,[this] { configureFileWatching(false); });
    });
    connect(m_diffWidget, &FileDiffWidget::operationFailed, this, &MainWindow::showError);
    connect(m_diffWidget, &FileDiffWidget::modifiedChanged, this, [this] { updateModifiedTitle(); });
    connect(m_diffWidget, &FileDiffWidget::saveRequested, this, [this](Side side) { saveSide(side == Side::Left); });
    m_dirWidget  = new DirDiffWidget(m_stack);
    // The desktop host permits filesystem operations; embedded widgets default read-only.
    m_dirWidget->setReadOnly(Side::Left,false); m_dirWidget->setReadOnly(Side::Right,false);
    connect(m_dirWidget,&DirDiffWidget::operationFailed,this,&MainWindow::showError);
    connect(m_dirWidget,&DirDiffWidget::copyRequested,this,[this](Side source,const QStringList& paths) {
        if(paths.isEmpty()) return;
        const Side destination=source==Side::Left ? Side::Right : Side::Left;
        if(m_dirWidget->isReadOnly(destination)) return;
        const auto plan=planDirectoryCopy(source==Side::Left ? m_dirWidget->leftPath() : m_dirWidget->rightPath(),
            destination==Side::Left ? m_dirWidget->leftPath() : m_dirWidget->rightPath(),paths);
        if(!plan.error.isEmpty()) { showError(plan.error); return; }
        QStringList entries;
        for(const auto& item:plan.items) entries.append(item.destination+(item.overwrites ? QStringLiteral(" [OVERWRITE]") : QString{}));
        if(QMessageBox::question(this,QStringLiteral("Copy selected entries"),
            QStringLiteral("Copy these entries? Existing files marked OVERWRITE will be replaced.\n\n%1").arg(entries.join('\n')),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        const auto error=executeDirectoryCopy(plan); if(!error.isEmpty()) showError(error);
        m_dirWidget->refresh();
    });
    connect(m_dirWidget,&DirDiffWidget::deleteRequested,this,[this](Side side,const QStringList& paths) {
        if(paths.isEmpty() || m_dirWidget->isReadOnly(side)) return;
        const auto root=side==Side::Left ? m_dirWidget->leftPath() : m_dirWidget->rightPath();
        QStringList absolute; for(const auto& path:paths) absolute.append(QDir(root).filePath(path));
        if(QMessageBox::question(this,QStringLiteral("Move selected entries to trash"),absolute.join('\n'),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        const auto error=trashDirectoryEntries(root,paths); if(!error.isEmpty()) showError(error);
        m_dirWidget->refresh();
    });
    for(auto* editor : {m_diffWidget->leftEditor(),m_diffWidget->rightEditor(),m_diffWidget->unifiedEditor()}) {
        auto* backspace=new QShortcut(Qt::Key_Backspace,editor);
        backspace->setContext(Qt::WidgetWithChildrenShortcut);
        connect(backspace,&QShortcut::activated,this,[this] {
            if(!m_dirWidget->leftPath().isEmpty() && confirmModified()) m_stack->setCurrentWidget(m_dirWidget);
        });
        connect(m_diffWidget, &FileDiffWidget::editableChanged, backspace, [editor, backspace] {
            backspace->setEnabled(editor->edit()->area()->readOnly());
        });
        connect(m_diffWidget, &FileDiffWidget::viewModeChanged, backspace, [editor, backspace] { backspace->setEnabled(editor->edit()->area()->readOnly()); });
        connect(m_diffWidget, &FileDiffWidget::unchangedLinesSkippedChanged, backspace, [editor, backspace] { backspace->setEnabled(editor->edit()->area()->readOnly()); });
    }

    auto* next = new QShortcut(Qt::Key_F7, m_diffWidget);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QShortcut::activated, m_diffWidget, &FileDiffWidget::navigateToNext);
    auto* previous = new QShortcut(Qt::SHIFT | Qt::Key_F7, m_diffWidget);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, m_diffWidget, &FileDiffWidget::navigateToPrev);
    auto* refresh=new QShortcut(Qt::Key_F5,m_diffWidget);
    refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refresh,&QShortcut::activated,this,&MainWindow::refreshFiles);
    auto* refreshBoth=new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")),this);
    connect(refreshBoth,&QShortcut::activated,this,[this] {
        if(m_stack->currentWidget()==m_diffWidget) refreshFiles(); else m_dirWidget->refresh();
    });
    for (Side source : {Side::Left, Side::Right}) {
        auto* copy = new QShortcut(source == Side::Left ? Qt::ALT | Qt::Key_Right : Qt::ALT | Qt::Key_Left, m_diffWidget);
        copy->setContext(Qt::WidgetWithChildrenShortcut);
        connect(copy, &QShortcut::activated, this, [this, source] {
            const auto current = m_diffWidget->currentChangeIndex();
            if (m_diffWidget->copyChange(current, source)) {
                connect(m_diffWidget, &FileDiffWidget::comparisonChanged, this, [this, current] {
                    m_diffWidget->navigateToChange(std::min(current, m_diffWidget->changeCount()-1));
                }, Qt::SingleShotConnection);
            }
        });
        const auto enabled = [this, source, copy] {
            copy->setEnabled(m_diffWidget->isEditable(source == Side::Left ? Side::Right : Side::Left) &&
                m_diffWidget->viewMode() == ViewMode::SideBySide && !m_diffWidget->unchangedLinesSkipped());
        };
        enabled();
        connect(m_diffWidget, &FileDiffWidget::editableChanged, copy, enabled);
        connect(m_diffWidget, &FileDiffWidget::viewModeChanged, copy, enabled);
        connect(m_diffWidget, &FileDiffWidget::unchangedLinesSkippedChanged, copy, enabled);
    }
    connect(m_diffWidget, &FileDiffWidget::loadFailed, this, &MainWindow::showError);
    connect(m_diffWidget, &FileDiffWidget::comparisonReplacementRequested, this, [this](const QString& left,const QString& right) {
        if (confirmModified()) loadFiles(left,right);
        else m_diffWidget->setPaths(m_diffWidget->saveTarget(Side::Left),m_diffWidget->saveTarget(Side::Right));
    });
    connect(m_diffWidget, &FileDiffWidget::fileBrowseRequested, this,
        [this](Side side, const QString& currentPath) {
            if (!confirmModified()) return;
            const auto path = QxFileDialog::getOpenFileName(this,
                side == Side::Left ? QStringLiteral("Select left file")
                                   : QStringLiteral("Select right file"), currentPath);
            if (!path.isEmpty()) m_diffWidget->setPath(side, path);
        });
    connect(m_dirWidget, &DirDiffWidget::directoryBrowseRequested, this,
        [this](Side side, const QString& currentPath) {
            const auto path = QxFileDialog::getExistingDirectory(this,
                side == Side::Left ? QStringLiteral("Select left directory")
                                   : QStringLiteral("Select right directory"), currentPath);
            if (!path.isEmpty()) m_dirWidget->setPath(side, path);
        });

    m_stack->addWidget(m_dirWidget);
    m_stack->addWidget(m_diffWidget);
    m_stack->setCurrentWidget(m_diffWidget);

    setCentralWidget(m_stack);
    setupMenus();
    resize(1200, 700);
    setWindowTitle(QStringLiteral("DiffMerge"));
    QTimer::singleShot(0, this, &MainWindow::offerSyntaxDownload);

    connect(m_dirWidget, &DirDiffWidget::fileActivated,
            this, &MainWindow::onFileActivated);
    connect(m_dirWidget, &DirDiffWidget::directoriesChanged,
            this, [this](const QString& l, const QString& r) {
        setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2").arg(l, r));
    });
    connect(m_diffWidget, &FileDiffWidget::pathsChanged,
            this, [this](const QString& l, const QString& r) {
        setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2").arg(l, r));
        updateModifiedTitle();
    });
    connect(m_diffWidget, &FileDiffWidget::backRequested,
            this, [this] {
        if (!confirmModified()) return;
        m_stack->setCurrentWidget(m_dirWidget);
        setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2")
                           .arg(m_dirWidget->leftPath(), m_dirWidget->rightPath()));
    });
    setEditMode(false, true);
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto* openFilesAction = fileMenu->addAction(QStringLiteral("&Open two files..."));
    openFilesAction->setShortcut(QKeySequence::Open);
    connect(openFilesAction, &QAction::triggered, this, &MainWindow::onOpenFiles);

    auto* openDirsAction = fileMenu->addAction(QStringLiteral("Open two &directories..."));
    openDirsAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
    connect(openDirsAction, &QAction::triggered, this, &MainWindow::onOpenDirectories);

    auto* inspect = fileMenu->addAction(QStringLiteral("Open conflict editor..."));
    inspect->setObjectName(QStringLiteral("inspectConflictMarkers"));
    connect(inspect, &QAction::triggered, this, &MainWindow::onInspectConflictMarkers);
    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction(QStringLiteral("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { saveSide(m_diffWidget->leftEditor()->edit()->area()->hasFocus()); });
    connect(fileMenu->addAction(QStringLiteral("Save Left")), &QAction::triggered, this, [this] { saveSide(true); });
    connect(fileMenu->addAction(QStringLiteral("Save Right")), &QAction::triggered, this, [this] { saveSide(false); });
    connect(fileMenu->addAction(QStringLiteral("Save Both")), &QAction::triggered, this, [this] {
        for (Side side : {Side::Left, Side::Right}) if (m_diffWidget->isModified(side) && !saveSide(side == Side::Left)) break;
    });
    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* swapSides = viewMenu->addAction(QStringLiteral("Swap sides"));
    swapSides->setObjectName(QStringLiteral("swapSides"));
    connect(swapSides, &QAction::triggered, this, [this] {
        if (m_stack->currentWidget() == m_dirWidget) {
            m_dirWidget->swapSides();
            setEditMode(!m_dirWidget->isReadOnly(Side::Left), !m_dirWidget->isReadOnly(Side::Right));
        } else if (m_diffWidget->swapSides()) {
            m_dirWidget->setReadOnly(Side::Left, !m_diffWidget->isEditable(Side::Left));
            m_dirWidget->setReadOnly(Side::Right, !m_diffWidget->isEditable(Side::Right));
        }
    });
    for (Side side : {Side::Left, Side::Right}) {
        auto* editable = viewMenu->addAction(side == Side::Left ? QStringLiteral("Edit left side") : QStringLiteral("Edit right side"));
        editable->setObjectName(side == Side::Left ? QStringLiteral("editLeft") : QStringLiteral("editRight"));
        editable->setCheckable(true);
        connect(editable, &QAction::toggled, this, [this, side](bool enabled) {
            m_diffWidget->setEditable(side, enabled);
            if (enabled || !m_diffWidget->isModified(side)) m_dirWidget->setReadOnly(side, !enabled);
        });
        connect(m_diffWidget, &FileDiffWidget::editableChanged, editable, [editable, side](Side changed, bool enabled) {
            if (side == changed) { QSignalBlocker blocker(editable); editable->setChecked(enabled); }
        });
    }
    viewMenu->addSeparator();
    auto* modes = new QActionGroup(this);
    modes->setExclusive(true);
    auto* sideBySide = viewMenu->addAction(QStringLiteral("Side by Side"));
    auto* unified = viewMenu->addAction(QStringLiteral("Unified"));
    for (auto* action : {sideBySide, unified}) { action->setCheckable(true); modes->addAction(action); }
    QSettings settings;
    m_diffWidget->setPanelSpacing(settings.value(QStringLiteral("view/panelSpacing"), 48).toInt());
    auto* spacingMenu = viewMenu->addMenu(QStringLiteral("Panel spacing"));
    auto* spacingRow = new QWidget(spacingMenu);
    auto* spacingLayout = new QHBoxLayout(spacingRow);
    auto* spacingSlider = new QSlider(Qt::Horizontal, spacingRow);
    spacingSlider->setObjectName(QStringLiteral("panelSpacingSlider"));
    spacingSlider->setRange(8, 160);
    spacingSlider->setValue(m_diffWidget->panelSpacing());
    spacingSlider->setMinimumWidth(200);
    auto* spacingLabel = new QLabel(QStringLiteral("%1 px").arg(spacingSlider->value()), spacingRow);
    spacingLayout->addWidget(spacingSlider);
    spacingLayout->addWidget(spacingLabel);
    auto* spacingAction = new QWidgetAction(spacingMenu);
    spacingAction->setDefaultWidget(spacingRow);
    spacingMenu->addAction(spacingAction);
    connect(spacingSlider, &QSlider::valueChanged, this, [this, spacingLabel](int pixels) {
        m_diffWidget->setPanelSpacing(pixels);
        spacingLabel->setText(QStringLiteral("%1 px").arg(pixels));
        QSettings().setValue(QStringLiteral("view/panelSpacing"), pixels);
    });
    connect(spacingMenu->addAction(QStringLiteral("Reset to 48 px")), &QAction::triggered,
            spacingSlider, [spacingSlider] { spacingSlider->setValue(48); });
    const auto mode = settings.value(QStringLiteral("view/unified"), false).toBool() ? ViewMode::Unified : ViewMode::SideBySide;
    m_diffWidget->setViewMode(mode);
    (mode == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
    connect(sideBySide, &QAction::triggered, this, [this, sideBySide, unified] {
        m_diffWidget->setViewMode(ViewMode::SideBySide);
        (m_diffWidget->viewMode() == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
    });
    connect(unified, &QAction::triggered, this, [this, sideBySide, unified] {
        m_diffWidget->setViewMode(ViewMode::Unified);
        (m_diffWidget->viewMode() == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
    });
    connect(m_diffWidget, &FileDiffWidget::viewModeChanged, this, [sideBySide, unified](ViewMode mode) {
        (mode == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
        QSettings().setValue(QStringLiteral("view/unified"), mode == ViewMode::Unified);
    });
    viewMenu->addSeparator();
    auto* skip = viewMenu->addAction(QStringLiteral("Skip unchanged lines"));
    skip->setCheckable(true);
    skip->setChecked(settings.value(QStringLiteral("view/skipUnchanged"), false).toBool());
    m_diffWidget->setUnchangedLinesSkipped(skip->isChecked());
    connect(skip, &QAction::toggled, this, [this,skip](bool enabled) {
        m_diffWidget->setUnchangedLinesSkipped(enabled);
        QSignalBlocker blocker(skip); skip->setChecked(m_diffWidget->unchangedLinesSkipped());
    });
    connect(m_diffWidget, &FileDiffWidget::unchangedLinesSkippedChanged, this, [skip](bool value) {
        skip->setChecked(value); QSettings().setValue(QStringLiteral("view/skipUnchanged"), value);
    });

    viewMenu->addSeparator();
    auto* wholeWords=viewMenu->addAction(QStringLiteral("Highlight whole changed words"));
    wholeWords->setObjectName(QStringLiteral("highlightWholeWords")); wholeWords->setCheckable(true);
    wholeWords->setChecked(settings.value(QStringLiteral("view/highlightWholeWords"),false).toBool());
    m_diffWidget->setHighlightDetail(wholeWords->isChecked() ? IntraLineDiffEngine::Detail::WholeWords : IntraLineDiffEngine::Detail::Characters);
    connect(wholeWords,&QAction::toggled,this,[this](bool enabled) {
        m_diffWidget->setHighlightDetail(enabled ? IntraLineDiffEngine::Detail::WholeWords : IntraLineDiffEngine::Detail::Characters);
        QSettings().setValue(QStringLiteral("view/highlightWholeWords"),enabled);
    });
    auto* refreshAction=viewMenu->addAction(QStringLiteral("Refresh (F5 files / Ctrl+R)"));
    connect(refreshAction,&QAction::triggered,this,[this] {
        if(m_stack->currentWidget()==m_diffWidget) refreshFiles(); else m_dirWidget->refresh();
    });
    const auto ignoreOption=[&](const QString& label, const QString& name, bool diffcore::DiffOptions::* field) {
        auto* action=viewMenu->addAction(label); action->setCheckable(true); action->setObjectName(name);
        const auto apply=[this,field](bool enabled) {
            auto options=m_diffWidget->diffOptions(); options.*field=enabled;
            m_diffWidget->setDiffOptions(options); m_dirWidget->setDiffOptions(options);
        };
        action->setChecked(settings.value(QStringLiteral("comparison/")+name,false).toBool());
        apply(action->isChecked());
        connect(action,&QAction::toggled,this,[apply,name](bool enabled) {
            apply(enabled); QSettings().setValue(QStringLiteral("comparison/")+name,enabled);
        });
    };
    ignoreOption(QStringLiteral("Ignore whitespace differences"),QStringLiteral("ignoreWhitespace"),&diffcore::DiffOptions::ignoreWhitespace);
    ignoreOption(QStringLiteral("Ignore trailing whitespace"),QStringLiteral("ignoreTrailingWhitespace"),&diffcore::DiffOptions::ignoreTrailingWhitespace);
    ignoreOption(QStringLiteral("Ignore case"),QStringLiteral("ignoreCase"),&diffcore::DiffOptions::ignoreCase);
    auto* differences=viewMenu->addAction(QStringLiteral("Show differences only (directories)"));
    differences->setCheckable(true);
    differences->setChecked(settings.value(QStringLiteral("directories/differencesOnly"),false).toBool());
    m_dirWidget->setDifferencesOnly(differences->isChecked());
    connect(differences,&QAction::toggled,this,[this](bool enabled) {
        m_dirWidget->setDifferencesOnly(enabled); QSettings().setValue(QStringLiteral("directories/differencesOnly"),enabled);
    });
    auto* emptyDirectories=viewMenu->addAction(QStringLiteral("Hide empty directories"));
    emptyDirectories->setCheckable(true);
    emptyDirectories->setChecked(settings.value(QStringLiteral("directories/hideEmpty"),false).toBool());
    m_dirWidget->setHideEmptyDirectories(emptyDirectories->isChecked());
    connect(emptyDirectories,&QAction::toggled,this,[this](bool enabled) {
        m_dirWidget->setHideEmptyDirectories(enabled); QSettings().setValue(QStringLiteral("directories/hideEmpty"),enabled);
    });
    auto* lineEndings=viewMenu->addAction(QStringLiteral("Ignore line endings (directories)"));
    lineEndings->setCheckable(true);
    lineEndings->setChecked(settings.value(QStringLiteral("directories/ignoreLineEndings"),false).toBool());
    m_dirWidget->setIgnoreLineEndings(lineEndings->isChecked());
    connect(lineEndings,&QAction::toggled,this,[this](bool enabled) {
        m_dirWidget->setIgnoreLineEndings(enabled); QSettings().setValue(QStringLiteral("directories/ignoreLineEndings"),enabled);
    });
    m_dirWidget->setExclusions(settings.value(QStringLiteral("directories/exclusions"),m_dirWidget->exclusions()).toStringList());
    auto* exclusions=viewMenu->addAction(QStringLiteral("Directory exclusions..."));
    connect(exclusions,&QAction::triggered,this,[this] {
        bool accepted=false;
        const auto text=QInputDialog::getMultiLineText(this,QStringLiteral("Directory exclusions"),
            QStringLiteral("One wildcard name pattern per line"),m_dirWidget->exclusions().join('\n'),&accepted);
        if(!accepted) return;
        QStringList patterns; for(const auto& line:text.split('\n')) if(!line.trimmed().isEmpty()) patterns.append(line.trimmed());
        m_dirWidget->setExclusions(patterns); QSettings().setValue(QStringLiteral("directories/exclusions"),patterns);
    });
    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    auto* syntaxAction = toolsMenu->addAction(QStringLiteral("Update Syntax Definitions..."));
    connect(syntaxAction, &QAction::triggered, this, &MainWindow::updateSyntaxData);
}

qce::kate::KateDataDownloader* MainWindow::syntaxDownloader() {
    if (m_syntaxDownloader) return m_syntaxDownloader;
    m_syntaxDownloader = new qce::kate::KateDataDownloader(this);
    connect(m_syntaxDownloader, &qce::kate::KateDataDownloader::progress, this,
        [this](int done, int total) {
            statusBar()->showMessage(QStringLiteral("Downloading syntax definitions: %1/%2").arg(done).arg(total));
        });
    connect(m_syntaxDownloader, &qce::kate::KateDataDownloader::finished, this,
        [this](bool ok, int downloaded, int failed) {
            m_diffWidget->reloadSyntaxDefinitions();
            statusBar()->showMessage(ok
                ? QStringLiteral("Syntax definitions up to date (%1 files downloaded)").arg(downloaded)
                : QStringLiteral("Syntax definitions incomplete: %1 downloaded, %2 failed").arg(downloaded).arg(failed), 8000);
        });
    return m_syntaxDownloader;
}

void MainWindow::offerSyntaxDownload() {
    if (!syntaxDownloader()->mustDownload()) return;
    QSettings settings;
    if (settings.value(QStringLiteral("syntaxDownloadDeclined"), false).toBool()) return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Syntax Definitions"),
        QStringLiteral("Download Kate syntax definitions %1 and color themes from kate-editor.org and invent.kde.org?\n\nThey will be stored in:\n%2")
            .arg(qce::kate::supportedSyntaxVersion().toString(), syntaxDownloader()->dataDir()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        settings.setValue(QStringLiteral("syntaxDownloadDeclined"), true);
        return;
    }
    updateSyntaxData();
}

void MainWindow::updateSyntaxData() {
    QSettings().remove(QStringLiteral("syntaxDownloadDeclined"));
    if (syntaxDownloader()->busy()) return;
    statusBar()->showMessage(QStringLiteral("Downloading syntax definitions…"));
    syntaxDownloader()->start();
}

void MainWindow::setFileLabels(const QStringList& labels) {
    if(labels.isEmpty()) return;
    for(int i=0;i<labels.size() && i<2;++i) {
        const Side side=i==0 ? Side::Left : Side::Right;
        const auto* editor=side==Side::Left ? m_diffWidget->leftEditor() : m_diffWidget->rightEditor();
        if(editor->syntaxLanguage().isEmpty()) m_diffWidget->setSyntaxFileName(side,QFileInfo(labels[i]).fileName());
    }
    setWindowTitle(QStringLiteral("DiffMerge — %1").arg(labels.join(QStringLiteral(" vs "))));
}

void MainWindow::showError(const QString& message) {
    QMessageBox::critical(this, QStringLiteral("Error"), message);
}

void MainWindow::onOpenFiles() {
    if (!confirmModified()) return;
    const QString left = QxFileDialog::getOpenFileName(
        this, QStringLiteral("Select left file"), {});
    if (left.isEmpty()) return;
    const QString right = QxFileDialog::getOpenFileName(
        this, QStringLiteral("Select right file"), {});
    if (right.isEmpty()) return;
    loadFiles(left, right);
}

void MainWindow::onInspectConflictMarkers() {
    const auto path = QxFileDialog::getOpenFileName(this, QStringLiteral("Open a text RESULT file (optional conflict markers)"), {});
    if (path.isEmpty()) return;
    MergeEditDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Conflict editor")); dialog.resize(1200,700);
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel(QStringLiteral("Loading RESULT…"), &dialog);
    status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(close);
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    diffcore::CancellationToken cancellation;
    auto* watcher = new QFutureWatcher<MergePreviewLoadResult>(&dialog);
    connect(watcher,&QFutureWatcher<MergePreviewLoadResult>::finished,&dialog,[&dialog,layout,status,watcher] {
        const auto loaded = watcher->result(); const auto& prepared = loaded.prepared;
        if (prepared.status != MergeSessionStatus::Ready || !loaded.stamp) { status->setText(prepared.message); return; }
        auto* preview = new MergeWidget(&dialog); dialog.merge = preview;
        connect(preview,&MergePreviewWidget::operationFailed,status,[status](const QString& message) { status->setText(message); status->show(); });
        layout->insertWidget(1,preview,1);
        auto* controls = new QWidget(&dialog); auto* row = new QHBoxLayout(controls);
        auto* markerSize = new QSpinBox(controls); markerSize->setRange(1,200000); markerSize->setValue(7);
        auto* spacing = new QSlider(Qt::Horizontal,controls); spacing->setRange(8,160); spacing->setValue(24);
        auto* editable = new QCheckBox(QStringLiteral("Edit RESULT"),controls); row->addWidget(editable);
        connect(editable,&QCheckBox::toggled,preview,&MergeWidget::setEditable);
        connect(preview,&MergeWidget::editableChanged,editable,[editable](bool enabled) { QSignalBlocker blocker(editable); editable->setChecked(enabled); });
        connect(preview,&MergeWidget::modifiedChanged,markerSize,[markerSize,preview](bool modified) {
            markerSize->setEnabled(!modified && preview->resultBytes() == std::optional<QByteArray>{preview->session()->inputs().resultSeed->file.bytes});
        });
        row->addWidget(new QLabel(QStringLiteral("Marker length"),controls)); row->addWidget(markerSize);
        row->addWidget(new QLabel(QStringLiteral("Panel spacing"),controls)); row->addWidget(spacing,1);
        layout->insertWidget(2,controls);
        auto* saveControls = new QWidget(&dialog); auto* saveRow = new QHBoxLayout(saveControls);
        auto* saveDraft = new QPushButton(QStringLiteral("Save draft"),saveControls);
        auto* saveResolved = new QPushButton(QStringLiteral("Save resolved"),saveControls);
        auto* finish = new QPushButton(QStringLiteral("Save resolved and close"),saveControls);
        auto* progress = new QProgressBar(saveControls); progress->hide();
        auto* cancelSave = new QPushButton(QStringLiteral("Cancel save"),saveControls); cancelSave->hide();
        saveRow->addWidget(saveDraft); saveRow->addWidget(saveResolved); saveRow->addWidget(finish);
        saveRow->addWidget(progress,1); saveRow->addWidget(cancelSave); layout->insertWidget(3,saveControls);
        const auto import = [preview,status,session=prepared.session](int size) {
            MarkerImportOptions options; options.markerSize=size; options.allowUnconfirmedMarkers=true;
            if (preview->setSession(session,options)) status->hide();
        };
        connect(markerSize,&QSpinBox::valueChanged,preview,import);
        connect(spacing,&QSlider::valueChanged,preview,&MergePreviewWidget::setPanelSpacing); import(7);
        const auto expected = std::make_shared<desktop::MergeSaveStamp>(*loaded.stamp);
        const auto updateButtons = [&dialog,preview,saveDraft,saveResolved,finish] {
            const bool writable = preview->isEditable() && !dialog.saving;
            saveDraft->setEnabled(writable); saveResolved->setEnabled(writable && preview->unresolvedCount()==0);
            finish->setEnabled(saveResolved->isEnabled());
        };
        connect(preview,&MergeWidget::editableChanged,&dialog,[updateButtons](bool) { updateButtons(); });
        connect(preview,&MergeWidget::conflictStatesChanged,&dialog,updateButtons); updateButtons();
        connect(cancelSave,&QPushButton::clicked,&dialog,[&dialog] { dialog.saveCancellation.requestCancellation(); });
        const auto startSave = [&dialog,preview,controls,status,progress,cancelSave,expected,updateButtons](MergeExportInput captured,MergeExportOptions options,bool closeAfter) {
            if (dialog.saving) return;
            dialog.closeAfterSave=false; dialog.saving=true; dialog.saveCancellation=diffcore::CancellationToken{};
            preview->setEnabled(false); controls->setEnabled(false); updateButtons();
            progress->setRange(0,0); progress->show(); cancelSave->show();
            status->setText(QStringLiteral("Validating and saving RESULT…")); status->show();
            auto* saving = new QFutureWatcher<desktop::MergeFileSaveResult>(&dialog);
            connect(saving,&QFutureWatcher<desktop::MergeFileSaveResult>::progressRangeChanged,progress,&QProgressBar::setRange);
            connect(saving,&QFutureWatcher<desktop::MergeFileSaveResult>::progressValueChanged,progress,&QProgressBar::setValue);
            connect(saving,&QFutureWatcher<desktop::MergeFileSaveResult>::finished,&dialog,[&dialog,preview,controls,status,progress,cancelSave,expected,updateButtons,saving,captured,closeAfter] {
                const auto result=saving->result(); saving->deleteLater(); dialog.saving=false;
                preview->setEnabled(true); controls->setEnabled(true); progress->hide(); cancelSave->hide();
                if (result.status != desktop::MergeSaveStatus::Saved || !result.stamp || !result.outcome) {
                    status->setText(result.message); updateButtons(); return;
                }
                *expected=*result.stamp;
                const bool acknowledged=preview->acknowledgeSaved(captured);
                status->setText(result.outcome->disposition==MergeExportDisposition::Resolved
                    ? QStringLiteral("Resolved RESULT saved. Repository state was not changed.")
                    : QStringLiteral("Draft saved. Conflicts remain unresolved."));
                updateButtons();
                if (closeAfter && acknowledged && !preview->isModified()) dialog.accept();
            });
            saving->setFuture(QtConcurrent::run([captured=std::move(captured),options=std::move(options),stamp=*expected,token=dialog.saveCancellation](QPromise<desktop::MergeFileSaveResult>& promise) {
                const auto progressCallback=[&promise](qint64 value,qint64 total) {
                    promise.setProgressRange(0,int(total)); promise.setProgressValue(int(value));
                };
                promise.addResult(desktop::saveMergeResultFile(captured,options,stamp,{},token,progressCallback));
            }));
        };
        connect(preview,&MergeWidget::saveRequested,&dialog,[&dialog,startSave](MergeExportInput input,MergeExportOptions options) { startSave(std::move(input),std::move(options),dialog.closeAfterSave); });
        connect(preview,&MergeWidget::finishRequested,&dialog,[startSave](MergeExportInput input,MergeExportOptions options) { startSave(std::move(input),std::move(options),true); });
        const auto requestSave = [&dialog,preview](MergeExportDisposition disposition,bool closeAfter) {
            if (dialog.saving) return false;
            if (!preview->isEditable()) {
                QMessageBox::information(&dialog,QStringLiteral("Read-only RESULT"),QStringLiteral("Enable Edit RESULT before saving.")); return false;
            }
            MergeExportOptions options; options.disposition=disposition;
            if (disposition==MergeExportDisposition::Draft && preview->unresolvedCount()>0) {
                if (QMessageBox::question(&dialog,QStringLiteral("Save unresolved draft?"),
                    QStringLiteral("Save %1 unresolved conflicts with markers? This does not finish the merge.").arg(preview->unresolvedCount()),
                    QMessageBox::Save | QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save) return false;
            }
            if (disposition==MergeExportDisposition::Resolved && !preview->session()->inputs().hostConflicts) {
                if (QMessageBox::question(&dialog,QStringLiteral("Confirm resolved result"),
                    QStringLiteral("No authoritative host conflict list is available. Have you reviewed RESULT and explicitly confirmed it as resolved?"),
                    QMessageBox::Yes | QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return false;
                options.confirmUnknownConflictState=true;
            }
            if (closeAfter && disposition==MergeExportDisposition::Resolved) return preview->requestFinish(options);
            return preview->requestSave(options);
        };
        connect(saveDraft,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Draft,false); });
        connect(saveResolved,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Resolved,false); });
        connect(finish,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Resolved,true); });
        dialog.saveAndMaybeClose=[&dialog,preview,requestSave](bool closeAfter) {
            const auto disposition=preview->unresolvedCount()>0 ? MergeExportDisposition::Draft : MergeExportDisposition::Resolved;
            dialog.closeAfterSave=closeAfter;
            const bool requested=requestSave(disposition,closeAfter);
            if (!requested) dialog.closeAfterSave=false;
            return requested;
        };
        auto* saveShortcut=new QShortcut(QKeySequence::Save,&dialog);
        connect(saveShortcut,&QShortcut::activated,&dialog,[&dialog] { dialog.saveAndMaybeClose(false); });
        auto* next=new QShortcut(Qt::Key_F7,&dialog); connect(next,&QShortcut::activated,preview,&MergePreviewWidget::navigateToNextConflict);
        auto* previous=new QShortcut(Qt::SHIFT | Qt::Key_F7,&dialog); connect(previous,&QShortcut::activated,preview,&MergePreviewWidget::navigateToPreviousConflict);
    });
    watcher->setFuture(QtConcurrent::run([path,cancellation] {
        MergePreviewLoadResult result;
        const auto loaded=desktop::readMergeResultFile(path,{},cancellation);
        if (loaded.status!=desktop::MergeSaveStatus::Saved || !loaded.stamp) {
            result.prepared.status=loaded.status==desktop::MergeSaveStatus::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::Error;
            result.prepared.message=loaded.message; return result;
        }
        MergeSessionInputs inputs; MergeResultSeed seed;
        seed.origin=ResultSeedOrigin::WorkingFile; seed.file.availability=MergeAvailability::Present;
        seed.file.bytes=loaded.bytes; seed.file.rawPath=QFile::encodeName(loaded.stamp->path);
        seed.file.label=loaded.stamp->path; seed.file.fileName=QFileInfo(path).fileName(); seed.fingerprint=loaded.stamp->fingerprint;
        inputs.resultSeed=std::move(seed);
        inputs.base.fileName=inputs.ours.fileName=inputs.theirs.fileName=QFileInfo(path).fileName();
        result.prepared=prepareMergeSession(inputs,{},cancellation); result.stamp=loaded.stamp; return result;
    }));
    dialog.exec(); cancellation.requestCancellation(); dialog.saveCancellation.requestCancellation();
}

void MainWindow::onOpenDirectories() {
    if (!confirmModified()) return;
    const QString left = QxFileDialog::getExistingDirectory(
        this, QStringLiteral("Select left directory"), {});
    if (left.isEmpty()) return;
    const QString right = QxFileDialog::getExistingDirectory(
        this, QStringLiteral("Select right directory"), {});
    if (right.isEmpty()) return;
    loadDirectories(left, right);
}

void MainWindow::loadFiles(const QString& leftPath, const QString& rightPath,
                           bool fromDir) {
    if (!confirmModified()) return;
    m_diffWidget->setBackVisible(fromDir);
    if (!m_diffWidget->loadFromPaths(leftPath, rightPath)) return;
    if (fromDir && (leftPath.isEmpty() || rightPath.isEmpty())) {
        const Side missing = leftPath.isEmpty() ? Side::Left : Side::Right;
        const auto relative = QDir(leftPath.isEmpty() ? m_dirWidget->rightPath() : m_dirWidget->leftPath()).relativeFilePath(leftPath.isEmpty() ? rightPath : leftPath);
        const auto target = QDir(leftPath.isEmpty() ? m_dirWidget->leftPath() : m_dirWidget->rightPath()).filePath(relative);
        m_diffWidget->setSaveTarget(missing, target);
        m_diffWidget->setPaths(leftPath.isEmpty() ? target : leftPath, rightPath.isEmpty() ? target : rightPath);
    }
    m_stack->setCurrentWidget(m_diffWidget);
}

void MainWindow::loadDirectories(const QString& leftPath, const QString& rightPath) {
    if (!confirmModified()) return;
    m_dirWidget->setDirectories(leftPath, rightPath);
    m_stack->setCurrentWidget(m_dirWidget);
}

void MainWindow::setEditMode(bool left, bool right) {
    m_diffWidget->setEditable(Side::Left, left); m_diffWidget->setEditable(Side::Right, right);
    m_dirWidget->setReadOnly(Side::Left, !left); m_dirWidget->setReadOnly(Side::Right, !right);
}
void MainWindow::setIgnoreOptions(bool whitespace, bool trailingWhitespace, bool caseInsensitive) {
    findChild<QAction*>(QStringLiteral("ignoreWhitespace"))->setChecked(whitespace);
    findChild<QAction*>(QStringLiteral("ignoreTrailingWhitespace"))->setChecked(trailingWhitespace);
    findChild<QAction*>(QStringLiteral("ignoreCase"))->setChecked(caseInsensitive);
}
void MainWindow::configureFileWatching(bool resetObserved) {
    QStringList watches;
    for(int i=0;i<2;++i) {
        const auto path=m_diffWidget->saveTarget(i==0 ? Side::Left : Side::Right);
        if(resetObserved || path!=m_watchedPaths[i]) m_observedFiles[i]=fileStamp(path);
        m_watchedPaths[i]=path;
        if(path.isEmpty()) continue;
        const QFileInfo info(path);
        if(info.exists()) {
            watches.append(info.absoluteFilePath());
            if(!info.canonicalFilePath().isEmpty()) watches.append(info.canonicalFilePath());
        }
        QStringList parents{info.absolutePath()};
        if(!info.canonicalFilePath().isEmpty()) parents.append(QFileInfo(info.canonicalFilePath()).absolutePath());
        for(auto parent : parents) {
            while(!QFileInfo(parent).isDir()) {
                const auto ancestor=QFileInfo(parent).absolutePath();
                if(ancestor==parent) break;
                parent=ancestor;
            }
            if(QFileInfo(parent).isDir()) watches.append(parent);
        }
    }
    watches.removeDuplicates();
    const auto existing=m_fileWatcher->files()+m_fileWatcher->directories();
    if(!existing.isEmpty()) m_fileWatcher->removePaths(existing);
    if(!watches.isEmpty()) m_fileWatcher->addPaths(watches);
}
void MainWindow::checkFileChanges() {
    if(m_stack->currentWidget()!=m_diffWidget) return;
    if(m_checkingFiles) { m_fileWatchTimer->start(); return; }
    QScopedValueRollback<bool> checking(m_checkingFiles,true);
    configureFileWatching(false);
    for(int i=0;i<2;++i) {
        const auto path=m_watchedPaths[i];
        const auto stamp=fileStamp(path);
        if(path.isEmpty() || stamp==m_observedFiles[i]) continue;
        m_observedFiles[i]=stamp;
        const Side side=i==0 ? Side::Left : Side::Right;
        bool discard=false;
        if(m_diffWidget->isModified(side)) {
            discard=QMessageBox::question(this,QStringLiteral("Compared file changed"),
                QStringLiteral("%1 changed on disk. Reload this side and discard its unsaved edits?\n"
                    "Choose No to keep your edits; saving will still check the external change.").arg(path),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
            if(!discard) continue;
        }
        if(m_diffWidget->reloadSide(side,discard)) m_observedFiles[i]=fileStamp(path);
    }
    configureFileWatching(false);
}
void MainWindow::refreshFiles() {
    if(m_checkingFiles) return;
    QScopedValueRollback<bool> checking(m_checkingFiles,true);
    for(int i=0;i<2;++i) {
        const Side side=i==0 ? Side::Left : Side::Right;
        const auto path=m_diffWidget->saveTarget(side);
        if(path.isEmpty() || (!QFileInfo::exists(path) && m_observedFiles[i]==QByteArrayLiteral("missing"))) continue;
        bool discard=false;
        if(m_diffWidget->isModified(side)) {
            discard=QMessageBox::question(this,QStringLiteral("Refresh compared file"),
                QStringLiteral("Reload %1 and discard this side's unsaved edits?").arg(path),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
            if(!discard) continue;
        }
        if(m_diffWidget->reloadSide(side,discard)) m_observedFiles[i]=fileStamp(path);
    }
    configureFileWatching(false);
}

bool MainWindow::saveSide(bool left) {
    const Side side = left ? Side::Left : Side::Right;
    if (m_diffWidget->saveTarget(side).isEmpty()) {
        const auto path = QxFileDialog::getSaveFileName(this, QStringLiteral("Save compared file"), {});
        if (path.isEmpty()) return false;
        m_diffWidget->setSaveTarget(side, path);
    }
    QString error;
    if (!m_diffWidget->save(side, &error)) {
        if (error == QStringLiteral("File changed on disk since loading or saving") &&
            QMessageBox::question(this, QStringLiteral("File changed on disk"),
                QStringLiteral("Overwrite the externally changed file %1?").arg(m_diffWidget->saveTarget(side)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            if (m_diffWidget->save(side, &error, true)) {
                m_observedFiles[left ? 0 : 1]=fileStamp(m_diffWidget->saveTarget(side));
                configureFileWatching(false); m_dirWidget->refresh(); return true;
            }
        }
        showError(error); return false;
    }
    m_observedFiles[left ? 0 : 1]=fileStamp(m_diffWidget->saveTarget(side));
    configureFileWatching(false); m_dirWidget->refresh(); return true;
}

bool MainWindow::confirmModified() {
    QStringList names;
    for (Side side : {Side::Left, Side::Right}) if (m_diffWidget->isModified(side))
        names.append(m_diffWidget->saveTarget(side).isEmpty() ? (side == Side::Left ? QStringLiteral("Left") : QStringLiteral("Right")) : m_diffWidget->saveTarget(side));
    if (names.isEmpty()) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Unsaved changes"),
        QStringLiteral("Save changes to:\n%1").arg(names.join('\n')), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        for (Side side : {Side::Left, Side::Right}) if (m_diffWidget->isModified(side) && !saveSide(side == Side::Left)) return false;
    } else m_diffWidget->discardChanges();
    return !m_diffWidget->isModified(Side::Left) && !m_diffWidget->isModified(Side::Right);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmModified()) event->accept(); else event->ignore();
}

void MainWindow::updateModifiedTitle() {
    QString title = windowTitle();
    if (title.startsWith(QStringLiteral("* "))) title.remove(0, 2);
    if (m_diffWidget->isModified(Side::Left) || m_diffWidget->isModified(Side::Right)) title.prepend(QStringLiteral("* "));
    setWindowTitle(title);
}

void MainWindow::prefillFiles(const QString& leftPath, const QString& rightPath) {
    m_diffWidget->setPaths(leftPath, rightPath);
    m_stack->setCurrentWidget(m_diffWidget);
}

void MainWindow::prefillDirs(const QString& leftPath, const QString& rightPath) {
    m_dirWidget->setPaths(leftPath, rightPath);
    m_stack->setCurrentWidget(m_dirWidget);
}

void MainWindow::onFileActivated(const QString& leftPath, const QString& rightPath) {
    loadFiles(leftPath, rightPath, /*fromDir=*/true);
}

}  // namespace diffmerge::gui
