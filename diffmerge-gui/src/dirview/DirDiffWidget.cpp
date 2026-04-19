#include "DirDiffWidget.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace diffmerge::gui {

namespace {

QColor colorForStatus(DirEntryStatus s) {
    switch (s) {
        case DirEntryStatus::OnlyLeft:   return QColor(0xff, 0xcc, 0xcc);  // light red
        case DirEntryStatus::OnlyRight:  return QColor(0xcc, 0xff, 0xcc);  // light green
        case DirEntryStatus::Different:  return QColor(0xff, 0xf0, 0x99);  // light yellow
        case DirEntryStatus::Same:       return QColor();                   // default
        case DirEntryStatus::Directory:  return QColor();                   // default
    }
    return {};
}

QString labelForStatus(DirEntryStatus s) {
    switch (s) {
        case DirEntryStatus::OnlyLeft:  return QStringLiteral("only left");
        case DirEntryStatus::OnlyRight: return QStringLiteral("only right");
        case DirEntryStatus::Different: return QStringLiteral("different");
        case DirEntryStatus::Same:      return QStringLiteral("same");
        case DirEntryStatus::Directory: return QStringLiteral("dir");
    }
    return {};
}

}  // namespace

DirDiffWidget::DirDiffWidget(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void DirDiffWidget::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setMargin(4);
    layout->addWidget(m_headerLabel);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Status")});

    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(true);
    m_view->setUniformRowHeights(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    const QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_view->setFont(f);

    layout->addWidget(m_view);

    connect(m_view, &QTreeView::activated,
            this,   &DirDiffWidget::onActivated);
}

void DirDiffWidget::setDirectories(const QString& leftPath,
                                   const QString& rightPath) {
    m_leftPath  = leftPath;
    m_rightPath = rightPath;
    m_headerLabel->setText(
        QStringLiteral("<b>Left:</b> %1&nbsp;&nbsp;&nbsp;<b>Right:</b> %2")
            .arg(leftPath.toHtmlEscaped(), rightPath.toHtmlEscaped()));

    m_entries = scanDirDiff(leftPath, rightPath);
    populate(m_entries);
}

void DirDiffWidget::populate(const QVector<DirDiffEntry>& entries) {
    m_model->removeRows(0, m_model->rowCount());

    static const QIcon folderIcon(QStringLiteral(":/icons/folder.svg"));

    // Stack of parent items indexed by depth; depth 0 uses invisibleRootItem.
    QVector<QStandardItem*> parentStack;
    parentStack.append(m_model->invisibleRootItem());

    for (int i = 0; i < entries.size(); ++i) {
        const DirDiffEntry& e = entries[i];

        // Trim the parent stack to the current depth
        while (parentStack.size() > e.depth + 1)
            parentStack.removeLast();

        const QString name = e.relativePath.section(QLatin1Char('/'), -1);

        auto* nameItem   = new QStandardItem(name);
        auto* statusItem = new QStandardItem(labelForStatus(e.status));

        nameItem->setData(i, Qt::UserRole);

        if (e.isDir) {
            nameItem->setIcon(folderIcon);
            QFont f = nameItem->font();
            f.setBold(true);
            nameItem->setFont(f);
        }

        const QColor bg = colorForStatus(e.status);
        if (bg.isValid()) {
            nameItem->setBackground(bg);
            statusItem->setBackground(bg);
        }

        parentStack.last()->appendRow({nameItem, statusItem});

        if (e.isDir)
            parentStack.append(nameItem);
    }

    m_view->expandAll();
}

void DirDiffWidget::onActivated(const QModelIndex& index) {
    const QModelIndex nameIdx = index.sibling(index.row(), 0);
    const int entryIdx = nameIdx.data(Qt::UserRole).toInt();
    if (entryIdx < 0 || entryIdx >= m_entries.size()) return;

    const DirDiffEntry& e = m_entries[entryIdx];
    if (e.isDir) return;

    emit fileActivated(e.leftPath, e.rightPath);
}

}  // namespace diffmerge::gui
