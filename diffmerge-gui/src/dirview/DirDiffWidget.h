#pragma once

#include <QWidget>

#include "DirDiffModel.h"

class QTreeView;
class QStandardItemModel;
class QLabel;

namespace diffmerge::gui {

class DirDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit DirDiffWidget(QWidget* parent = nullptr);

    void setDirectories(const QString& leftPath, const QString& rightPath);

signals:
    // Emitted when the user activates a file entry (double-click / Enter).
    // One or both paths may be empty (OnlyLeft / OnlyRight cases).
    void fileActivated(const QString& leftFilePath, const QString& rightFilePath);

private slots:
    void onActivated(const QModelIndex& index);

private:
    void setupUi();
    void populate(const QVector<DirDiffEntry>& entries);

    QLabel*            m_headerLabel = nullptr;
    QTreeView*         m_view        = nullptr;
    QStandardItemModel* m_model      = nullptr;
    QVector<DirDiffEntry> m_entries;
};

}  // namespace diffmerge::gui
