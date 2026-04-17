#include "MainWindow.h"

#include <QFile>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

#include "fileview/FileDiffWidget.h"

namespace diffmerge::gui {

namespace {

// Load a text file into a QStringList. Returns empty list on failure.
// Errors are reported via the caller (we don't have QMessageBox here).
QStringList loadTextFile(const QString& path, QString* errorOut) {
    QStringList lines;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Cannot open %1: %2")
                            .arg(path, f.errorString());
        }
        return lines;
    }
    QTextStream in(&f);
    while (!in.atEnd()) lines.append(in.readLine());
    return lines;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_diffWidget = new FileDiffWidget(this);
    setCentralWidget(m_diffWidget);
    setupMenus();
    resize(1200, 700);
    setWindowTitle(QStringLiteral("DiffMerge"));
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto* openAction = fileMenu->addAction(QStringLiteral("&Open two files..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFiles);

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

void MainWindow::loadFiles(const QString& leftPath, const QString& rightPath) {
    QString err;
    const QStringList leftLines = loadTextFile(leftPath, &err);
    if (!err.isEmpty()) { showError(err); return; }
    const QStringList rightLines = loadTextFile(rightPath, &err);
    if (!err.isEmpty()) { showError(err); return; }

    m_diffWidget->setContent(leftLines, rightLines);
    setWindowTitle(QStringLiteral("DiffMerge — %1 vs %2")
                       .arg(leftPath, rightPath));
}

}  // namespace diffmerge::gui
