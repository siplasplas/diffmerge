#pragma once

#include <QWidget>

#include "DirDiffModel.h"

class QTreeView;
class QStandardItemModel;
class QLineEdit;
class QToolButton;

namespace diffmerge::gui {

class DirDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit DirDiffWidget(QWidget* parent = nullptr);

    void setDirectories(const QString& leftPath, const QString& rightPath);

    QString leftPath()  const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }

signals:
    void fileActivated(const QString& leftFilePath, const QString& rightFilePath);
    // Emitted after reload so MainWindow can update its title.
    void directoriesChanged(const QString& leftPath, const QString& rightPath);

private slots:
    void onActivated(const QModelIndex& index);
    void onBrowseLeft();
    void onBrowseRight();
    void reload();

private:
    void setupUi();
    void populate(const QVector<DirDiffEntry>& entries);

    QLineEdit*          m_leftPathEdit  = nullptr;
    QToolButton*        m_leftBrowse    = nullptr;
    QLineEdit*          m_rightPathEdit = nullptr;
    QToolButton*        m_rightBrowse   = nullptr;
    QTreeView*          m_view          = nullptr;
    QStandardItemModel* m_model         = nullptr;
    QVector<DirDiffEntry> m_entries;
    QString m_leftPath;
    QString m_rightPath;
};

}  // namespace diffmerge::gui
