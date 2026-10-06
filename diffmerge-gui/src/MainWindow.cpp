#include "MainWindow.h"

#include <qxfiledialog.h>
#include <QShortcut>
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
#include <qce/kate/KateDataDownloader.h>

#include <diffmerge/DirDiffWidget.h>
#include <diffmerge/FileDiffWidget.h>

namespace diffmerge::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_stack      = new QStackedWidget(this);
    m_diffWidget = new FileDiffWidget(m_stack);
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
            if (m_diffWidget->save(side, &error, true)) { m_dirWidget->refresh(); return true; }
        }
        showError(error); return false;
    }
    m_dirWidget->refresh(); return true;
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
