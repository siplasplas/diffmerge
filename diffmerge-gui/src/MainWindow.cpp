#include "MainWindow.h"

#include <QFile>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

#include "dirview/DirDiffWidget.h"
#include "fileview/FileDiffWidget.h"

namespace diffmerge::gui {

namespace {

QStringList loadTextFile(const QString& path, QString* errorOut) {
    QStringList lines;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());
        return lines;
    }
    QTextStream in(&f);
    while (!in.atEnd()) lines.append(in.readLine());
    return lines;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_stack      = new QStackedWidget(this);
    m_diffWidget = new FileDiffWidget(m_stack);
    m_dirWidget  = new DirDiffWidget(m_stack);

    m_stack->addWidget(m_dirWidget);
    m_stack->addWidget(m_diffWidget);
    m_stack->setCurrentWidget(m_diffWidget);

    setCentralWidget(m_stack);
    setupMenus();
    resize(1200, 700);
    setWindowTitle(QStringLiteral("DiffMerge"));

    connect(m_dirWidget, &DirDiffWidget::fileActivated,
            this, &MainWindow::onFileActivated);
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
}

void MainWindow::showError(const QString& message) {
    QMessageBox::critical(this, QStringLiteral("Error"), message);
}

void MainWindow::onOpenFiles() {
    const QString left = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select left file"));
    if (left.isEmpty()) return;
    const QString right = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select right file"));
    if (right.isEmpty()) return;
    loadFiles(left, right);
}

void MainWindow::onOpenDirectories() {
    const QString left = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select left directory"));
    if (left.isEmpty()) return;
    const QString right = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select right directory"));
    if (right.isEmpty()) return;
    loadDirectories(left, right);
}

void MainWindow::loadFiles(const QString& leftPath, const QString& rightPath) {
    QString err;
    const QStringList leftLines = loadTextFile(leftPath, &err);
    if (!err.isEmpty()) { showError(err); return; }
    const QStringList rightLines = loadTextFile(rightPath, &err);
    if (!err.isEmpty()) { showError(err); return; }

    m_diffWidget->setContent(leftLines, rightLines);
    m_stack->setCurrentWidget(m_diffWidget);
    setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2").arg(leftPath, rightPath));
}

void MainWindow::loadDirectories(const QString& leftPath, const QString& rightPath) {
    m_dirWidget->setDirectories(leftPath, rightPath);
    m_stack->setCurrentWidget(m_dirWidget);
    setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2").arg(leftPath, rightPath));
}

void MainWindow::onFileActivated(const QString& leftPath, const QString& rightPath) {
    if (leftPath.isEmpty() || rightPath.isEmpty()) {
        // One side missing — show a read-only single-file view in the future;
        // for now just inform the user.
        showError(QStringLiteral("File exists only on one side:\n%1\n%2")
                      .arg(leftPath, rightPath));
        return;
    }
    loadFiles(leftPath, rightPath);
}

}  // namespace diffmerge::gui
