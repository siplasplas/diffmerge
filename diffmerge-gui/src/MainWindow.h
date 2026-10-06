#ifndef DIFFMERGE_GUI_MAINWINDOW_H
#define DIFFMERGE_GUI_MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>

namespace qce::kate { class KateDataDownloader; }

namespace diffmerge::gui {

class FileDiffWidget;
class DirDiffWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);

    void chooseFiles() { onOpenFiles(); }
    void setFileLabels(const QStringList& labels);
    void setEditMode(bool left, bool right);
    void setIgnoreOptions(bool whitespace, bool trailingWhitespace, bool caseInsensitive);
    void loadFiles(const QString& leftPath, const QString& rightPath,
                   bool fromDir = false);
    void loadDirectories(const QString& leftPath, const QString& rightPath);

    // Show view and pre-fill paths without triggering a diff/scan.
    void prefillFiles(const QString& leftPath, const QString& rightPath = {});
    void prefillDirs(const QString& leftPath,  const QString& rightPath = {});

private slots:
    void onOpenFiles();
    void onOpenDirectories();
    void onFileActivated(const QString& leftPath, const QString& rightPath);

private:
    bool confirmModified();
    bool saveSide(bool left);
    void updateModifiedTitle();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void setupMenus();
    void showError(const QString& message);
    void offerSyntaxDownload();
    void updateSyntaxData();
    qce::kate::KateDataDownloader* syntaxDownloader();

    qce::kate::KateDataDownloader* m_syntaxDownloader = nullptr;
    QStackedWidget* m_stack      = nullptr;
    FileDiffWidget* m_diffWidget = nullptr;
    DirDiffWidget*  m_dirWidget  = nullptr;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_MAINWINDOW_H
