#ifndef DIFFMERGE_GUI_MAINWINDOW_H
#define DIFFMERGE_GUI_MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>

namespace diffmerge::gui {

class FileDiffWidget;
class DirDiffWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);

    void loadFiles(const QString& leftPath, const QString& rightPath);
    void loadDirectories(const QString& leftPath, const QString& rightPath);

private slots:
    void onOpenFiles();
    void onOpenDirectories();
    void onFileActivated(const QString& leftPath, const QString& rightPath);

private:
    void setupMenus();
    void showError(const QString& message);

    QStackedWidget* m_stack      = nullptr;
    FileDiffWidget* m_diffWidget = nullptr;
    DirDiffWidget*  m_dirWidget  = nullptr;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_MAINWINDOW_H
