#include "MainWindow.h"

#include <qxfiledialog.h>
#include <QShortcut>
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
    m_diffWidget->loadFromPaths(leftPath, rightPath);
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
    if (leftPath.isEmpty() || rightPath.isEmpty()) {
        showError(QStringLiteral("File exists only on one side:\n%1\n%2")
                      .arg(leftPath, rightPath));
        return;
    }
    loadFiles(leftPath, rightPath, /*fromDir=*/true);
}

}  // namespace diffmerge::gui
