// Minimal main window for stage 2. Lets the user open two files and see
// their diff. No merge, no directory comparison - just the file view.

#ifndef DIFFMERGE_GUI_MAINWINDOW_H
#define DIFFMERGE_GUI_MAINWINDOW_H

#include <QMainWindow>

namespace diffmerge::gui {

class FileDiffWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);

    // If non-null, load these two files immediately (used when the app is
    // launched with two file paths on the command line).
    void loadFiles(const QString& leftPath, const QString& rightPath);

private slots:
    void onOpenFiles();

private:
    void setupMenus();
    void showError(const QString& message);

    FileDiffWidget* m_diffWidget;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_MAINWINDOW_H
