#include <diffmerge/DirDiffWidget.h>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QPromise>
#include <QProgressBar>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QStyle>
#include <QStandardItemModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

static void initializeDiffMergeResources() { Q_INIT_RESOURCE(diffmerge_widgets); }
namespace diffmerge::gui {
namespace {
QString labelForStatus(const DirDiffEntry& entry) {
    QString label;
    switch (entry.status) {
        case DirEntryStatus::OnlyLeft: label = QStringLiteral("only left"); break;
        case DirEntryStatus::OnlyRight: label = QStringLiteral("only right"); break;
        case DirEntryStatus::Different: label = QStringLiteral("different"); break;
        case DirEntryStatus::Same: label = QStringLiteral("same"); break;
        case DirEntryStatus::Directory: label = QStringLiteral("directory"); break;
        case DirEntryStatus::Error: return QStringLiteral("read error");
    }
    if (!entry.contentVerified) label += QStringLiteral(" (metadata only)");
    return label;
}
}
DirDiffWidget::DirDiffWidget(QWidget* parent) : QWidget(parent) { initializeDiffMergeResources(); setupUi(); }
DirDiffWidget::~DirDiffWidget() { m_cancellation.requestCancellation(); }
void DirDiffWidget::setupUi() {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* paths = new QHBoxLayout;
    const auto addPath = [&](Side side, QLineEdit*& edit) {
        edit = new QLineEdit(this);
        edit->setPlaceholderText(side == Side::Left ? QStringLiteral("Left directory...") : QStringLiteral("Right directory..."));
        paths->addWidget(edit);
        auto* browse = new QToolButton(this); browse->setIcon(QIcon(QStringLiteral(":/icons/folder.svg")));
        browse->setToolTip(QStringLiteral("Browse directory")); paths->addWidget(browse);
        connect(browse, &QToolButton::clicked, this, [this, side, edit] { emit directoryBrowseRequested(side, edit->text()); });
        connect(edit, &QLineEdit::returnPressed, this, &DirDiffWidget::reload);
    };
    addPath(Side::Left,m_leftPathEdit); addPath(Side::Right,m_rightPathEdit); layout->addLayout(paths);
    auto* actions = new QHBoxLayout;
    const auto button = [&](const QString& text) { auto* b = new QToolButton(this); b->setText(text); actions->addWidget(b); return b; };
    auto* up = button(QStringLiteral("↑ Parent")); connect(up,&QToolButton::clicked,this,&DirDiffWidget::navigateUp);
    auto* refreshButton = button(QStringLiteral("Refresh")); connect(refreshButton,&QToolButton::clicked,this,&DirDiffWidget::refresh);
    m_copyRight = button(QStringLiteral("Copy →")); m_copyLeft = button(QStringLiteral("← Copy"));
    m_deleteLeft = button(QStringLiteral("Delete left")); m_deleteRight = button(QStringLiteral("Delete right"));
    connect(m_copyRight,&QToolButton::clicked,this,[this] { if(!m_rightReadOnly) emit copyRequested(Side::Left,selectedPaths()); });
    connect(m_copyLeft,&QToolButton::clicked,this,[this] { if(!m_leftReadOnly) emit copyRequested(Side::Right,selectedPaths()); });
    connect(m_deleteLeft,&QToolButton::clicked,this,[this] { if(!m_leftReadOnly) emit deleteRequested(Side::Left,selectedPaths()); });
    connect(m_deleteRight,&QToolButton::clicked,this,[this] { if(!m_rightReadOnly) emit deleteRequested(Side::Right,selectedPaths()); });
    actions->addStretch(); layout->addLayout(actions);
    auto* scanning=new QHBoxLayout;
    m_scanProgress=new QProgressBar(this); m_scanProgress->setRange(0,1000); m_scanProgress->hide();
    m_cancelScan=new QToolButton(this); m_cancelScan->setText(QStringLiteral("Cancel scan"));
    m_cancelScan->setObjectName(QStringLiteral("cancelDirectoryScan")); m_cancelScan->hide();
    connect(m_cancelScan,&QToolButton::clicked,this,&DirDiffWidget::cancelScan);
    scanning->addWidget(m_scanProgress); scanning->addWidget(m_cancelScan); layout->addLayout(scanning);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({QStringLiteral("Name"),QStringLiteral("Left size"),QStringLiteral("Left modified"),
        QStringLiteral("Status"),QStringLiteral("Right size"),QStringLiteral("Right modified")});
    m_view = new QTableView(this); m_view->setObjectName(QStringLiteral("directoryTable")); m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows); m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers); m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->installEventFilter(this); layout->addWidget(m_view,1);
    connect(m_view,&QTableView::activated,this,&DirDiffWidget::onActivated);
    connect(m_view->selectionModel(),&QItemSelectionModel::selectionChanged,this,[this] { updateActions(); });
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view,&QWidget::customContextMenuRequested,this,[this](const QPoint& point) {
        QMenu menu(this);
        for(auto* button : {m_copyRight,m_copyLeft,m_deleteLeft,m_deleteRight}) {
            auto* action=menu.addAction(button->text()); action->setEnabled(button->isEnabled());
            connect(action,&QAction::triggered,button,&QToolButton::click);
        }
        menu.exec(m_view->viewport()->mapToGlobal(point));
    });
    updateActions();
}
void DirDiffWidget::setPaths(const QString& left, const QString& right) { m_leftPathEdit->setText(left); m_rightPathEdit->setText(right); }
void DirDiffWidget::setDirectories(const QString& left, const QString& right) { setPaths(left,right); reload(); }
void DirDiffWidget::setPath(Side side, const QString& path) { (side == Side::Left ? m_leftPathEdit : m_rightPathEdit)->setText(path); reload(); }
void DirDiffWidget::reload() {
    const auto left=m_leftPathEdit->text().trimmed(), right=m_rightPathEdit->text().trimmed();
    if(left.isEmpty() || right.isEmpty()) return;
    if(!QFileInfo(left).isDir() || !QFileInfo(right).isDir()) { emit operationFailed(QStringLiteral("Select two existing directories")); return; }
    m_leftPath=QDir(left).absolutePath(); m_rightPath=QDir(right).absolutePath(); m_relative.clear(); m_selectName.clear();
    emit currentDirectoryChanged(m_relative); refresh();
}
QString DirDiffWidget::currentPath(Side side) const { return QDir(side == Side::Left ? m_leftPath : m_rightPath).filePath(m_relative); }
void DirDiffWidget::swapSides() {
    std::swap(m_leftPath, m_rightPath);
    std::swap(m_leftReadOnly, m_rightReadOnly);
    const auto path = m_leftPathEdit->text();
    m_leftPathEdit->setText(m_rightPathEdit->text()); m_rightPathEdit->setText(path);
    updateActions();
    emit directoriesChanged(currentPath(Side::Left), currentPath(Side::Right));
    refresh();
}

void DirDiffWidget::refresh() {
    if(m_leftPath.isEmpty() || m_rightPath.isEmpty()) return;
    if(m_selectName.isEmpty() && m_view->currentIndex().isValid()) m_selectName=m_view->currentIndex().siblingAtColumn(0).data().toString();
    m_cancellation.requestCancellation(); m_cancellation=diffcore::CancellationToken{};
    const auto token=m_cancellation; const auto generation=++m_generation;
    const auto left=m_leftPath, right=m_rightPath; const auto options=m_options;
    m_scanning=true; m_view->setEnabled(false); updateActions();
    m_scanProgress->setValue(0); m_scanProgress->setFormat(QStringLiteral("Scanning…")); m_scanProgress->show(); m_cancelScan->show();
    auto* watcher=new QFutureWatcher<DirectoryScanResult>(this);
    connect(watcher,&QFutureWatcher<DirectoryScanResult>::finished,this,[this,watcher,generation] {
        const auto result=watcher->result(); watcher->deleteLater();
        if(generation!=m_generation) return;
        m_scanning=false; m_view->setEnabled(true);
        m_scanProgress->hide(); m_cancelScan->hide();
        if(result.status == DirectoryScanStatus::Ready) { m_entries=result.entries; populate(); }
        else if(result.status != DirectoryScanStatus::Cancelled) { m_entries.clear(); populate(); emit operationFailed(result.message); }
        updateActions(); emit scanFinished();
    });
    connect(watcher,&QFutureWatcher<DirectoryScanResult>::progressValueChanged,this,[this,watcher,generation](int value) {
        if(generation!=m_generation) return;
        m_scanProgress->setValue(value); m_scanProgress->setFormat(watcher->future().progressText());
    });
    watcher->setFuture(QtConcurrent::run([left,right,options,token](QPromise<DirectoryScanResult>& promise) {
        promise.setProgressRange(0,1000);
        promise.addResult(scanDirectories(left,right,options,token,[&promise](const QString& name,qint64 done,qint64 total) {
            promise.setProgressValueAndText(total>0 ? int(1000.0L*done/total) : 0,
                total>0 ? QStringLiteral("%1: %2 / %3 bytes").arg(name).arg(done).arg(total) : QStringLiteral("Scanning %1").arg(name));
        }));
    }));
}
void DirDiffWidget::cancelScan() { m_cancellation.requestCancellation(); }
void DirDiffWidget::populate() {
    m_model->removeRows(0,m_model->rowCount());
    if(!m_relative.isEmpty()) {
        auto* parent=new QStandardItem(QStringLiteral("..")); parent->setData(-1,Qt::UserRole);
        parent->setIcon(style()->standardIcon(QStyle::SP_ArrowUp)); m_model->appendRow(parent);
    }
    for(int i=0;i<m_entries.size();++i) {
        const auto& e=m_entries[i];
        const auto parent=e.relativePath.contains('/') ? e.relativePath.left(e.relativePath.lastIndexOf('/')) : QString{};
        if(parent!=m_relative || (m_differencesOnly && e.status==DirEntryStatus::Same)) continue;
        if(m_hideEmptyDirectories && e.emptyDirectory) continue;
        const auto name=e.relativePath.section('/',-1);
        const auto size=[](const QString& path,qint64 bytes,bool directory) { return path.isEmpty() || directory ? QString{} : QString::number(bytes); };
        QList<QStandardItem*> row;
        for(const auto& text : QStringList{name,size(e.leftPath,e.leftSize,e.isDir),e.leftModified.toString(Qt::ISODate),labelForStatus(e),
                size(e.rightPath,e.rightSize,e.isDir),e.rightModified.toString(Qt::ISODate)}) row.append(new QStandardItem(text));
        row[0]->setData(i,Qt::UserRole); if(e.isDir) row[0]->setIcon(QIcon(QStringLiteral(":/icons/folder.svg")));
        const QColor background=e.status == DirEntryStatus::Same ? QColor(Qt::white) :
            e.status == DirEntryStatus::OnlyRight ? QColor(204,255,204) : e.status == DirEntryStatus::OnlyLeft ? QColor(255,204,204) : QColor(255,240,153);
        for(auto* item:row) { item->setBackground(background); item->setForeground(QColor(Qt::black)); }
        if(!e.diagnostic.isEmpty()) for(auto* item:row) item->setToolTip(e.diagnostic);
        m_model->appendRow(row);
    }
    for(int i=0;i<m_model->rowCount();++i) if(m_model->index(i,0).data().toString()==m_selectName) { m_view->setCurrentIndex(m_model->index(i,0)); break; }
    if(!m_view->currentIndex().isValid() && m_model->rowCount()) m_view->setCurrentIndex(m_model->index(0,0));
    m_selectName.clear(); m_leftPathEdit->setText(currentPath(Side::Left)); m_rightPathEdit->setText(currentPath(Side::Right));
    emit directoriesChanged(currentPath(Side::Left),currentPath(Side::Right));
}
bool DirDiffWidget::navigateInto(const QString& name) {
    for(const auto& entry:m_entries) {
        const auto relative=m_relative.isEmpty() ? name : m_relative+'/'+name;
        if(entry.relativePath!=relative || !entry.isDir) continue;
        for(const auto& path:{entry.leftPath,entry.rightPath}) if(!path.isEmpty() && (!QFileInfo(path).isDir() || QFileInfo(path).isSymLink())) {
            emit operationFailed(QStringLiteral("Cannot enter a file/directory type conflict")); return false;
        }
        m_relative=relative; m_selectName.clear(); populate(); emit currentDirectoryChanged(m_relative); return true;
    }
    return false;
}
void DirDiffWidget::navigateUp() {
    if(m_relative.isEmpty()) return;
    m_selectName=m_relative.section('/',-1);
    m_relative=m_relative.contains('/') ? m_relative.left(m_relative.lastIndexOf('/')) : QString{};
    populate(); emit currentDirectoryChanged(m_relative);
}
void DirDiffWidget::onActivated(const QModelIndex& index) {
    if(!index.isValid() || m_scanning) return;
    const int number=index.siblingAtColumn(0).data(Qt::UserRole).toInt();
    if(number<0) { navigateUp(); return; }
    if(number>=m_entries.size()) return;
    const auto& entry=m_entries[number];
    if(entry.isDir) navigateInto(entry.relativePath.section('/',-1));
    else emit fileActivated(entry.leftPath,entry.rightPath);
}
bool DirDiffWidget::eventFilter(QObject* watched,QEvent* event) {
    if(watched==m_view && event->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Backspace) { navigateUp(); return true; }
        if(key->key()==Qt::Key_F5) { if(m_copyRight->isEnabled()) m_copyRight->click(); return true; }
        if(key->key()==Qt::Key_R && key->modifiers()==Qt::ControlModifier) { refresh(); return true; }
        if(key->key()==Qt::Key_Right && key->modifiers()==Qt::AltModifier) { m_copyRight->click(); return true; }
        if(key->key()==Qt::Key_Left && key->modifiers()==Qt::AltModifier) { m_copyLeft->click(); return true; }
    }
    return QWidget::eventFilter(watched,event);
}
QStringList DirDiffWidget::selectedPaths() const {
    QStringList paths;
    for(const auto& index:m_view->selectionModel()->selectedRows()) {
        const int number=index.data(Qt::UserRole).toInt(); if(number>=0 && number<m_entries.size()) paths.append(m_entries[number].relativePath);
    }
    return paths;
}
void DirDiffWidget::updateActions() {
    const bool selected=!m_scanning && !selectedPaths().isEmpty();
    bool hasLeft=selected, hasRight=selected;
    for(const auto& index:m_view->selectionModel()->selectedRows()) {
        const int number=index.data(Qt::UserRole).toInt();
        if(number<0 || number>=m_entries.size()) continue;
        hasLeft &= !m_entries[number].leftPath.isEmpty();
        hasRight &= !m_entries[number].rightPath.isEmpty();
    }
    m_copyRight->setEnabled(hasLeft && !m_rightReadOnly); m_copyLeft->setEnabled(hasRight && !m_leftReadOnly);
    m_deleteLeft->setEnabled(hasLeft && !m_leftReadOnly); m_deleteRight->setEnabled(hasRight && !m_rightReadOnly);
}
void DirDiffWidget::setReadOnly(Side side,bool readOnly) { (side==Side::Left ? m_leftReadOnly : m_rightReadOnly)=readOnly; updateActions(); }
void DirDiffWidget::setDifferencesOnly(bool enabled) { m_differencesOnly=enabled; populate(); updateActions(); }
void DirDiffWidget::setHideEmptyDirectories(bool enabled) { m_hideEmptyDirectories=enabled; populate(); updateActions(); }
void DirDiffWidget::setIgnoreLineEndings(bool enabled) {
    if(m_options.ignoreLineEndings==enabled) return;
    m_options.ignoreLineEndings=enabled; refresh();
}
void DirDiffWidget::setExclusions(const QStringList& patterns) { m_options.exclusions=patterns; refresh(); }
void DirDiffWidget::setDiffOptions(const diffcore::DiffOptions& options) { m_options.diff=options; refresh(); }
}
