#pragma once
#include <QWidget>
#include <diffmerge/DirDiffModel.h>
#include <diffmerge/AlignedLineModel.h>
class QTableView;
class QStandardItemModel;
class QLineEdit;
class QToolButton;
namespace diffmerge::gui {
class DirDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit DirDiffWidget(QWidget* parent = nullptr);
    ~DirDiffWidget() override;
    void setDirectories(const QString& leftPath, const QString& rightPath);
    void setPaths(const QString& leftPath, const QString& rightPath);
    void setPath(Side side, const QString& path);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    QString currentRelativeDirectory() const { return m_relative; }
    bool navigateInto(const QString& name);
    bool isScanning() const { return m_scanning; }
    void setDifferencesOnly(bool enabled);
    bool differencesOnly() const { return m_differencesOnly; }
    void setHideEmptyDirectories(bool enabled);
    bool hideEmptyDirectories() const { return m_hideEmptyDirectories; }
    void setIgnoreLineEndings(bool enabled);
    bool ignoreLineEndings() const { return m_options.ignoreLineEndings; }
    void setDiffOptions(const diffcore::DiffOptions& options);
    diffcore::DiffOptions diffOptions() const { return m_options.diff; }
    void setExclusions(const QStringList& patterns);
    QStringList exclusions() const { return m_options.exclusions; }
    void setReadOnly(Side side, bool readOnly);
    bool isReadOnly(Side side) const { return side == Side::Left ? m_leftReadOnly : m_rightReadOnly; }
public slots:
    void refresh();
    void navigateUp();
signals:
    void directoryBrowseRequested(Side side, const QString& currentPath);
    void fileActivated(const QString& leftFilePath, const QString& rightFilePath);
    void directoriesChanged(const QString& leftPath, const QString& rightPath);
    void currentDirectoryChanged(const QString& relativeDirectory);
    void scanFinished();
    void operationFailed(const QString& message);
    void copyRequested(Side source, const QStringList& relativePaths);
    void deleteRequested(Side side, const QStringList& relativePaths);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void setupUi();
    void populate();
    void reload();
    void onActivated(const QModelIndex& index);
    QStringList selectedPaths() const;
    QString currentPath(Side side) const;
    void updateActions();
    QLineEdit *m_leftPathEdit = nullptr, *m_rightPathEdit = nullptr;
    QTableView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;
    QToolButton *m_copyLeft = nullptr, *m_copyRight = nullptr, *m_deleteLeft = nullptr, *m_deleteRight = nullptr;
    QVector<DirDiffEntry> m_entries;
    QString m_leftPath, m_rightPath, m_relative, m_selectName;
    DirectoryScanOptions m_options;
    diffcore::CancellationToken m_cancellation;
    quint64 m_generation = 0;
    bool m_scanning = false, m_differencesOnly = false;
    bool m_hideEmptyDirectories = false;
    bool m_leftReadOnly = true, m_rightReadOnly = true;
};
}
