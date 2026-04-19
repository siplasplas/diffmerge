#include "DirDiffWidget.h"

#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace diffmerge::gui {

namespace {

QColor colorForStatus(DirEntryStatus s) {
    switch (s) {
        case DirEntryStatus::OnlyLeft:   return QColor(0xff, 0xcc, 0xcc);
        case DirEntryStatus::OnlyRight:  return QColor(0xcc, 0xff, 0xcc);
        case DirEntryStatus::Different:  return QColor(0xff, 0xf0, 0x99);
        case DirEntryStatus::Same:       return QColor();
        case DirEntryStatus::Directory:  return QColor();
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
    auto* vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    // Path bar
    static const QIcon folderIcon(QStringLiteral(":/icons/folder.svg"));

    auto* pathBar    = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathBar);
    pathLayout->setContentsMargins(4, 3, 4, 3);

    m_leftPathEdit = new QLineEdit(pathBar);
    m_leftPathEdit->setPlaceholderText(QStringLiteral("Left directory..."));
    m_leftBrowse = new QToolButton(pathBar);
    m_leftBrowse->setIcon(folderIcon);
    m_leftBrowse->setToolTip(QStringLiteral("Browse left directory"));
    m_leftBrowse->setAutoRaise(true);

    m_rightPathEdit = new QLineEdit(pathBar);
    m_rightPathEdit->setPlaceholderText(QStringLiteral("Right directory..."));
    m_rightBrowse = new QToolButton(pathBar);
    m_rightBrowse->setIcon(folderIcon);
    m_rightBrowse->setToolTip(QStringLiteral("Browse right directory"));
    m_rightBrowse->setAutoRaise(true);

    pathLayout->addWidget(m_leftPathEdit);
    pathLayout->addWidget(m_leftBrowse);
    pathLayout->addSpacing(8);
    pathLayout->addWidget(m_rightPathEdit);
    pathLayout->addWidget(m_rightBrowse);

    vLayout->addWidget(pathBar);

    // Tree view
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
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    vLayout->addWidget(m_view);

    connect(m_leftBrowse,   &QToolButton::clicked, this, &DirDiffWidget::onBrowseLeft);
    connect(m_rightBrowse,  &QToolButton::clicked, this, &DirDiffWidget::onBrowseRight);
    connect(m_leftPathEdit,  &QLineEdit::returnPressed, this, &DirDiffWidget::reload);
    connect(m_rightPathEdit, &QLineEdit::returnPressed, this, &DirDiffWidget::reload);
    connect(m_view, &QTreeView::activated, this, &DirDiffWidget::onActivated);
}

void DirDiffWidget::setDirectories(const QString& leftPath,
                                   const QString& rightPath) {
    m_leftPathEdit->setText(leftPath);
    m_rightPathEdit->setText(rightPath);
    reload();
}

void DirDiffWidget::reload() {
    m_leftPath  = m_leftPathEdit->text().trimmed();
    m_rightPath = m_rightPathEdit->text().trimmed();
    if (m_leftPath.isEmpty() || m_rightPath.isEmpty()) return;

    m_entries = scanDirDiff(m_leftPath, m_rightPath);
    populate(m_entries);
    emit directoriesChanged(m_leftPath, m_rightPath);
}

void DirDiffWidget::onBrowseLeft() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select left directory"), m_leftPathEdit->text());
    if (!dir.isEmpty()) {
        m_leftPathEdit->setText(dir);
        reload();
    }
}

void DirDiffWidget::onBrowseRight() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select right directory"), m_rightPathEdit->text());
    if (!dir.isEmpty()) {
        m_rightPathEdit->setText(dir);
        reload();
    }
}

void DirDiffWidget::populate(const QVector<DirDiffEntry>& entries) {
    m_model->removeRows(0, m_model->rowCount());

    static const QIcon folderIcon(QStringLiteral(":/icons/folder.svg"));

    QVector<QStandardItem*> parentStack;
    parentStack.append(m_model->invisibleRootItem());

    for (int i = 0; i < entries.size(); ++i) {
        const DirDiffEntry& e = entries[i];

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
