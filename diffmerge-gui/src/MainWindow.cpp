#include "MainWindow.h"

#include <qxfiledialog.h>
#include <QShortcut>
#include <QInputDialog>
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
            if(!m_dirWidget->leftPath().isEmpty()) m_stack->setCurrentWidget(m_dirWidget);
        });
    }

    auto* next = new QShortcut(Qt::Key_F7, m_diffWidget);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QShortcut::activated, m_diffWidget, &FileDiffWidget::navigateToNext);
    auto* previous = new QShortcut(Qt::SHIFT | Qt::Key_F7, m_diffWidget);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, m_diffWidget, &FileDiffWidget::navigateToPrev);
    connect(m_diffWidget, &FileDiffWidget::loadFailed, this, &MainWindow::showError);
    connect(m_diffWidget, &FileDiffWidget::fileBrowseRequested, this,
        [this](Side side, const QString& currentPath) {
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
        m_stack->setCurrentWidget(m_dirWidget);
        setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2")
                           .arg(m_dirWidget->leftPath(), m_dirWidget->rightPath()));
    });
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

    auto* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* modes = new QActionGroup(this);
    modes->setExclusive(true);
    auto* sideBySide = viewMenu->addAction(QStringLiteral("Side by Side"));
    auto* unified = viewMenu->addAction(QStringLiteral("Unified"));
    for (auto* action : {sideBySide, unified}) { action->setCheckable(true); modes->addAction(action); }
    QSettings settings;
    const auto mode = settings.value(QStringLiteral("view/unified"), false).toBool() ? ViewMode::Unified : ViewMode::SideBySide;
    m_diffWidget->setViewMode(mode);
    (mode == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
    connect(sideBySide, &QAction::triggered, this, [this] { m_diffWidget->setViewMode(ViewMode::SideBySide); });
    connect(unified, &QAction::triggered, this, [this] { m_diffWidget->setViewMode(ViewMode::Unified); });
    connect(m_diffWidget, &FileDiffWidget::viewModeChanged, this, [sideBySide, unified](ViewMode mode) {
        (mode == ViewMode::Unified ? unified : sideBySide)->setChecked(true);
        QSettings().setValue(QStringLiteral("view/unified"), mode == ViewMode::Unified);
    });
    viewMenu->addSeparator();
    auto* skip = viewMenu->addAction(QStringLiteral("Skip unchanged lines"));
    skip->setCheckable(true);
    skip->setChecked(settings.value(QStringLiteral("view/skipUnchanged"), false).toBool());
    m_diffWidget->setUnchangedLinesSkipped(skip->isChecked());
    connect(skip, &QAction::toggled, m_diffWidget, &FileDiffWidget::setUnchangedLinesSkipped);
    connect(m_diffWidget, &FileDiffWidget::unchangedLinesSkippedChanged, this, [skip](bool value) {
        skip->setChecked(value); QSettings().setValue(QStringLiteral("view/skipUnchanged"), value);
    });

    viewMenu->addSeparator();
    auto* differences=viewMenu->addAction(QStringLiteral("Show differences only (directories)"));
    differences->setCheckable(true);
    differences->setChecked(settings.value(QStringLiteral("directories/differencesOnly"),false).toBool());
    m_dirWidget->setDifferencesOnly(differences->isChecked());
    connect(differences,&QAction::toggled,this,[this](bool enabled) {
        m_dirWidget->setDifferencesOnly(enabled); QSettings().setValue(QStringLiteral("directories/differencesOnly"),enabled);
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
    const QString left = QxFileDialog::getOpenFileName(
        this, QStringLiteral("Select left file"), {});
    if (left.isEmpty()) return;
    const QString right = QxFileDialog::getOpenFileName(
        this, QStringLiteral("Select right file"), {});
    if (right.isEmpty()) return;
    loadFiles(left, right);
}

void MainWindow::onOpenDirectories() {
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
    m_diffWidget->setBackVisible(fromDir);
    if(leftPath.isEmpty() || rightPath.isEmpty()) {
        const auto path=leftPath.isEmpty() ? rightPath : leftPath;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) { showError(file.errorString()); return; }
        if(file.size()>8*1024*1024) { showError(QStringLiteral("File exceeds the text preview byte limit")); return; }
        const auto bytes=file.readAll();
        if(file.error()!=QFileDevice::NoError) { showError(file.errorString()); return; }
        if(bytes.contains('\0')) { showError(QStringLiteral("Binary files cannot be displayed as text")); return; }
        QStringDecoder decoder(QStringDecoder::Utf8);
        const auto text=decoder(bytes);
        if(decoder.hasError()) { showError(QStringLiteral("File is not valid UTF-8")); return; }
        const auto source=TextSnapshot::fromText(text,path,path);
        auto prepared=leftPath.isEmpty() ? prepareComparison({},source) : prepareComparison(source,{});
        if(prepared.status!=PreparationStatus::Ready) { showError(prepared.message); return; }
        m_diffWidget->setPaths(leftPath,rightPath); m_diffWidget->setComparison(prepared.comparison);
    } else m_diffWidget->loadFromPaths(leftPath, rightPath);
    m_stack->setCurrentWidget(m_diffWidget);
}

void MainWindow::loadDirectories(const QString& leftPath, const QString& rightPath) {
    m_dirWidget->setDirectories(leftPath, rightPath);
    m_stack->setCurrentWidget(m_dirWidget);
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
