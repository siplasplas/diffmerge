#include <diffmerge/ConflictResolverWidget.h>
#include <diffmerge/MergeWidget.h>
#include <diffmerge/DiffEditor.h>
#include <qce/CodeEditArea.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <functional>

namespace diffmerge::gui {
using namespace diffcore;
namespace {
struct Analysis { ResolutionPlan plan; std::shared_ptr<const PreparedMergeSession> session; QString error; };
class PlanCommand : public QUndoCommand {
public:
    PlanCommand(ResolutionPlan before,ResolutionPlan after,std::function<void(const ResolutionPlan&)> install)
        : m_before(std::move(before)),m_after(std::move(after)),m_install(std::move(install)) {}
    void undo() override { m_install(m_before); }
    void redo() override { m_install(m_after); }
private:
    ResolutionPlan m_before,m_after;
    std::function<void(const ResolutionPlan&)> m_install;
};
// Convert native normalized UTF-16 editor offsets to retained UTF-8/EOL byte offsets.
qint64 byteOffset(const QByteArray& bytes,int units) {
    qint64 start=bytes.startsWith(QByteArray::fromHex("efbbbf")) ? 3:0;
    while(start<bytes.size()) {
        qint64 end=start;
        while(end<bytes.size() && bytes[end]!='\n' && bytes[end]!='\r') ++end;
        const QString text=QString::fromUtf8(bytes.mid(start,end-start));
        if(units<=text.size()) return start+text.left(units).toUtf8().size();
        units-=text.size();
        if(end<bytes.size()) { if(bytes[end++]=='\r' && end<bytes.size() && bytes[end]=='\n') ++end; --units; }
        start=end;
    }
    return bytes.size();
}
}
struct ConflictResolverState {
    MergeWidget* merge;
    QListWidget* list;
    QCheckBox* showAutomatic;
    QComboBox *policy,*target,*candidates;
    QLabel *summary,*reason;
    QPlainTextEdit* preview;
    QProgressBar* progress;
    QPushButton *cancel,*analyze,*apply,*defer,*accept;
    QFutureWatcher<Analysis>* watcher;
    CancellationToken cancellation;
    ResolutionPlan plan;
    QByteArray original;
    QString fileName;
    MarkerOptions markers;
    quint64 revision=0;
    bool running=false,refreshing=false,installing=false,selecting=false;
};
ConflictResolverWidget::ConflictResolverWidget(QWidget* parent) : QWidget(parent),m_state(std::make_unique<ConflictResolverState>()) {
    auto& s=*m_state; auto* layout=new QVBoxLayout(this); auto* controls=new QHBoxLayout;
    s.policy=new QComboBox(this); s.policy->addItems({"Replay","Conservative"});
    s.target=new QComboBox(this); s.target->addItems({"RIGHT target","LEFT target"});
    s.analyze=new QPushButton("Discard decisions and analyze",this);
    s.showAutomatic=new QCheckBox("Show automatic decisions",this);
    controls->addWidget(s.policy); controls->addWidget(s.target); controls->addWidget(s.analyze); controls->addWidget(s.showAutomatic);
    s.summary=new QLabel(this); controls->addWidget(s.summary,1); layout->addLayout(controls);
    s.progress=new QProgressBar(this); s.progress->hide(); s.cancel=new QPushButton("Cancel analysis",this); s.cancel->hide();
    auto* progressRow=new QHBoxLayout; progressRow->addWidget(s.progress); progressRow->addWidget(s.cancel); layout->addLayout(progressRow);
    auto* split=new QSplitter(this); s.list=new QListWidget(split); s.list->setMaximumWidth(320);
    s.merge=new MergeWidget(split); s.merge->setDecisionActionsVisible(false); s.merge->setViewMode(MergeViewMode::Conflicts); split->setStretchFactor(1,1); layout->addWidget(split,1);
    s.reason=new QLabel(this); s.reason->setWordWrap(true); s.reason->setTextFormat(Qt::PlainText); layout->addWidget(s.reason);
    auto* actions=new QHBoxLayout; s.candidates=new QComboBox(this); actions->addWidget(s.candidates,1);
    s.apply=new QPushButton("Apply candidate",this); s.defer=new QPushButton("Keep for review",this); s.accept=new QPushButton("Accept current fragment",this);
    actions->addWidget(s.apply); actions->addWidget(s.defer); actions->addWidget(s.accept);
    auto* reviewRange=new QPushButton("Use selected RESULT range",this); actions->addWidget(reviewRange); layout->addLayout(actions);
    connect(reviewRange,&QPushButton::clicked,this,[this] {
        auto& state=*m_state; if(state.running) return;
        const auto bytes=state.merge->resultBytes(); if(!bytes) return;
        QString text=QString::fromUtf8(*bytes);
        if(text.startsWith(QChar(0xfeff))) text.remove(0,1);
        text.replace("\r\n","\n"); text.replace(QChar('\r'),QChar('\n'));
        const auto offset=[&text](const auto& position) {
            int start=0; for(int line=0;line<position.line;++line) {
                const int next=text.indexOf(QChar('\n'),start); if(next<0) return int(text.size()); start=next+1;
            }
            return start+position.column;
        };
        auto* area=state.merge->resultEditor()->edit()->area();
        const int begin=offset(area->selectionStart()),end=offset(area->selectionEnd());
        state.merge->reviewConflictRange(state.merge->currentConflictIndex(),{begin,end-begin});
    });
    s.preview=new QPlainTextEdit(this); s.preview->setReadOnly(true); s.preview->setMaximumHeight(140); layout->addWidget(s.preview);
    s.watcher=new QFutureWatcher<Analysis>(this);
    connect(s.watcher,&QFutureWatcher<Analysis>::progressRangeChanged,s.progress,&QProgressBar::setRange);
    connect(s.watcher,&QFutureWatcher<Analysis>::progressValueChanged,s.progress,&QProgressBar::setValue);
    connect(s.cancel,&QPushButton::clicked,this,&ConflictResolverWidget::cancelAnalysis);
    connect(s.showAutomatic,&QCheckBox::toggled,this,[this] { refresh(); });
    connect(s.list,&QListWidget::currentRowChanged,this,[this](int row) {
        auto* item=m_state->list->item(row); if(item) selectDecision(item->data(Qt::UserRole).toInt());
    });
    connect(s.candidates,&QComboBox::currentIndexChanged,this,[this](int index) {
        auto& state=*m_state; const int conflict=state.merge->currentConflictIndex();
        if(conflict>=0 && conflict<state.plan.decisions.size() && index>=0 && index<state.plan.decisions[conflict].candidates.size()) {
            const auto& c=state.plan.decisions[conflict].candidates[index];
            state.preview->setPlainText(QString::fromUtf8(c.replacement));
            state.reason->setText(state.plan.decisions[conflict].explanation+"\n"+state.plan.decisions[conflict].reasons.join(", ")+"\n"+c.assumptions.join("\n"));
        }
    });
    connect(s.apply,&QPushButton::clicked,this,[this] { applyCandidate(m_state->merge->currentConflictIndex(),m_state->candidates->currentData().toString()); });
    connect(s.defer,&QPushButton::clicked,this,[this] { applyCandidate(m_state->merge->currentConflictIndex(),m_state->candidates->currentData().toString(),true); });
    connect(s.accept,&QPushButton::clicked,this,[this] { acceptCurrentText(m_state->merge->currentConflictIndex()); });
    connect(s.analyze,&QPushButton::clicked,this,[this] {
        auto& state=*m_state; state.merge->discardChanges();
        ResolutionOptions options; options.policy=state.policy->currentIndex() ? ResolutionPolicy::Conservative:ResolutionPolicy::Replay;
        options.target=state.target->currentIndex() ? ResolutionTarget::Left:ResolutionTarget::Right;
        setInput(state.original,state.fileName,state.markers,options);
    });
    connect(s.merge,&MergePreviewWidget::currentConflictChanged,this,[this](int index) { if(!m_state->selecting) selectDecision(index); });
    connect(s.merge,&MergeWidget::conflictStatesChanged,this,[this] { QTimer::singleShot(0,this,[this] { refresh(); }); });
    connect(s.merge,&MergeWidget::modifiedChanged,this,[this](bool modified) { if(modified && m_state->running) cancelAnalysis(); });
    connect(s.merge,&MergeWidget::operationFailed,this,&ConflictResolverWidget::operationFailed);
    connect(s.watcher,&QFutureWatcher<Analysis>::finished,this,[this] {
        auto& state=*m_state; const auto analysis=state.watcher->result(); state.running=false; state.progress->hide(); state.cancel->hide();
        state.analyze->setEnabled(true);
        if(state.cancellation.isCancellationRequested() || state.merge->isModified()) {
            emit operationFailed("Analysis cancelled or RESULT changed; the result was not applied"); emit analysisFinished(false); return;
        }
        if(analysis.plan.status!=ConflictStatus::Complete || !analysis.session) {
            emit operationFailed(analysis.error.isEmpty() ? analysis.plan.message:analysis.error); emit analysisFinished(false); return;
        }
        state.installing=true;
        if(!state.merge->setSession(analysis.session,{state.markers.markerSize,true,state.markers.literalMarkerLines})) { state.installing=false; emit analysisFinished(false); return; }
        state.merge->setEditable(true);
        auto before=analysis.plan;
        for(auto& d:before.decisions) { d.state=DecisionState::NeedsReview; d.replacement.reset(); d.sourceSlices.clear(); }
        state.plan=before;
        auto* stack=undoStack(); stack->beginMacro("Automatically resolve compatible conflicts");
        bool success=true;
        for(int i=0;i<analysis.plan.decisions.size();++i) if(analysis.plan.decisions[i].replacement)
            if(!state.merge->replaceConflictText(i,*analysis.plan.decisions[i].replacement)) { success=false; break; }
        stack->push(new PlanCommand(before,analysis.plan,[this](const ResolutionPlan& plan) { m_state->plan=plan; QTimer::singleShot(0,this,[this] { refresh(); }); }));
        stack->endMacro();
        if(!success) { stack->undo(); emit operationFailed("An automatic replacement could not be applied"); }
        state.installing=false; refresh();
        const auto conflicts=state.merge->conflicts();
        for(int i=0;i<conflicts.size();++i) if(conflicts[i].state!=MergeResolutionState::Resolved) { selectDecision(i); break; }
        emit analysisFinished(success);
    });
    refresh();
}
ConflictResolverWidget::~ConflictResolverWidget() { m_state->cancellation.requestCancellation(); undoStack()->clear(); }
bool ConflictResolverWidget::setInput(const QByteArray& bytes,const QString& fileName,const MarkerOptions& markers,const ResolutionOptions& options) {
    auto& s=*m_state;
    if(s.running || s.merge->isModified()) { emit operationFailed("Discard current edits before replacing the input"); return false; }
    s.original=bytes; s.fileName=fileName; s.markers=markers; ++s.revision;
    s.policy->setCurrentIndex(options.policy==ResolutionPolicy::Conservative ? 1:0); s.target->setCurrentIndex(options.target==ResolutionTarget::Left ? 1:0);
    s.running=true; s.cancellation=CancellationToken{}; s.progress->setRange(0,0); s.progress->show(); s.cancel->show(); s.analyze->setEnabled(false);
    s.watcher->setFuture(QtConcurrent::run([bytes,fileName,markers,options,token=s.cancellation](QPromise<Analysis>& promise) {
        Analysis result;
        result.plan=planConflictResolution(bytes,markers,options,{},token,[&promise](int value,int total) { promise.setProgressRange(0,total); promise.setProgressValue(value); });
        if(result.plan.status==ConflictStatus::Complete) {
            MergeSessionInputs inputs; MergeResultSeed seed; seed.file.availability=MergeAvailability::Present;
            seed.file.bytes=bytes; seed.file.fileName=fileName; inputs.resultSeed=seed;
            inputs.base.fileName=inputs.ours.fileName=inputs.theirs.fileName=fileName;
            const auto prepared=prepareMergeSession(inputs,{},token); result.session=prepared.session; result.error=prepared.message;
        }
        promise.addResult(result);
    }));
    return true;
}
void ConflictResolverWidget::cancelAnalysis() { m_state->cancellation.requestCancellation(); }
bool ConflictResolverWidget::isAnalyzing() const { return m_state->running; }
bool ConflictResolverWidget::isModified() const { return m_state->merge->isModified(); }
MergeWidget* ConflictResolverWidget::mergeEditor() const { return m_state->merge; }
QUndoStack* ConflictResolverWidget::undoStack() const { return m_state->merge->resultEditor()->edit()->area()->undoStack(); }
const ResolutionPlan& ConflictResolverWidget::plan() const { return m_state->plan; }
std::optional<QByteArray> ConflictResolverWidget::resultBytes() const { return m_state->merge->resultBytes(); }
int ConflictResolverWidget::pendingDecisionCount() const { return m_state->merge->unresolvedCount(); }
void ConflictResolverWidget::selectDecision(int index) {
    auto& s=*m_state; if(s.selecting || index<0 || index>=s.plan.decisions.size()) return;
    s.selecting=true; s.merge->navigateToConflict(index); s.candidates->clear();
    for(const auto& c:s.plan.decisions[index].candidates) s.candidates->addItem(c.title,c.id);
    if(s.candidates->count()>0) s.candidates->setCurrentIndex(0);
    s.apply->setEnabled(s.candidates->count()>0); s.defer->setEnabled(s.candidates->count()>0); s.accept->setEnabled(true); s.selecting=false;
}
void ConflictResolverWidget::refresh() {
    auto& s=*m_state; if(s.refreshing || s.installing) return;
    s.refreshing=true; const int selected=s.merge->currentConflictIndex(); QSignalBlocker block(s.list); s.list->clear();
    const auto conflicts=s.merge->conflicts(); int policies=0;
    for(int i=0;i<s.plan.decisions.size();++i) {
        const auto& d=s.plan.decisions[i]; const bool pending=i>=conflicts.size() || conflicts[i].state!=MergeResolutionState::Resolved;
        if(d.state==DecisionState::AutomaticPolicy) ++policies;
        if(!pending && !s.showAutomatic->isChecked()) continue;
        const auto name=pending ? (d.state==DecisionState::Deferred ? QString("deferred"):QString("needs-review")):decisionStateName(d.state);
        auto* item=new QListWidgetItem(QStringLiteral("%1: %2").arg(i+1).arg(name),s.list); item->setData(Qt::UserRole,i);
        if(i==selected) s.list->setCurrentItem(item);
    }
    s.summary->setText(QStringLiteral("%1 pending; %2 policy decisions").arg(s.merge->unresolvedCount()).arg(policies));
    s.refreshing=false; emit stateChanged();
}
bool ConflictResolverWidget::applyCandidate(int index,const QString& candidateId,bool deferred) {
    auto& s=*m_state; if(s.running || index<0 || index>=s.plan.decisions.size()) return false;
    for(const auto& c:s.plan.decisions[index].candidates) if(c.id==candidateId) {
        auto after=s.plan; QString error;
        if(!reviewConflictDecision(after,s.plan.input.sha256,s.plan.decisions[index].id,c.replacement,deferred,&error)) { emit operationFailed(error); return false; }
        auto* stack=undoStack(); stack->beginMacro(deferred ? "Keep conflict for review":"Accept conflict candidate");
        const bool ok=s.merge->replaceConflictText(index,c.replacement,!deferred);
        if(ok) stack->push(new PlanCommand(s.plan,after,[this](const ResolutionPlan& plan) { m_state->plan=plan; QTimer::singleShot(0,this,[this] { refresh(); }); }));
        stack->endMacro(); refresh(); return ok;
    }
    return false;
}
bool ConflictResolverWidget::acceptCurrentText(int index) {
    auto& s=*m_state; const auto conflicts=s.merge->conflicts(); const auto bytes=s.merge->resultBytes();
    if(s.running || !bytes || index<0 || index>=conflicts.size() || index>=s.plan.decisions.size() || !conflicts[index].mapped) return false;
    const auto range=conflicts[index].range;
    const auto start=byteOffset(*bytes,range.start),end=byteOffset(*bytes,range.start+range.length);
    const QByteArray fragment=bytes->mid(start,end-start);
    const auto markers=parseConflictFile(fragment,{s.markers.markerSize,{} });
    if(markers.status!=ConflictStatus::Complete || !markers.file.conflicts.isEmpty()) { emit operationFailed("Remove conflict markers before accepting current text"); return false; }
    auto after=s.plan; if(!reviewConflictDecision(after,s.plan.input.sha256,after.decisions[index].id,fragment)) return false;
    auto* stack=undoStack(); stack->beginMacro("Accept edited conflict fragment");
    const bool ok=s.merge->markConflictResolved(index);
    if(ok) stack->push(new PlanCommand(s.plan,after,[this](const ResolutionPlan& plan) { m_state->plan=plan; QTimer::singleShot(0,this,[this] { refresh(); }); }));
    stack->endMacro(); refresh(); return ok;
}
void ConflictResolverWidget::navigateToNextPending() {
    const auto conflicts=m_state->merge->conflicts(); const int current=m_state->merge->currentConflictIndex();
    for(int step=1;step<=conflicts.size();++step) { const int i=(std::max(current,0)+step)%conflicts.size(); if(conflicts[i].state!=MergeResolutionState::Resolved) { selectDecision(i); return; } }
}
void ConflictResolverWidget::navigateToPreviousPending() {
    const auto conflicts=m_state->merge->conflicts(); const int current=m_state->merge->currentConflictIndex();
    for(int step=1;step<=conflicts.size();++step) { const int i=(std::max(current,0)+conflicts.size()-step)%conflicts.size(); if(conflicts[i].state!=MergeResolutionState::Resolved) { selectDecision(i); return; } }
}
QJsonObject ConflictResolverWidget::report() const {
    auto plan=m_state->plan; MaterializedResolution output; const auto bytes=resultBytes();
    if(!bytes) { output.status=ConflictStatus::Error; output.message="RESULT cannot be serialized"; return resolutionReport(plan,output); }
    output.status=ConflictStatus::Complete; output.bytes=*bytes; output.clean=pendingDecisionCount()==0;
    const auto conflicts=m_state->merge->conflicts();
    for(int i=0;i<plan.decisions.size() && i<conflicts.size();++i) {
        const auto range=conflicts[i].range; const auto start=byteOffset(*bytes,range.start),end=byteOffset(*bytes,range.start+range.length);
        output.conflictRanges.append({start,end-start});
        if(conflicts[i].state!=MergeResolutionState::Resolved && plan.decisions[i].state!=DecisionState::Deferred) plan.decisions[i].state=DecisionState::NeedsReview;
        if(!conflicts[i].mapped) { plan.decisions[i].reasons.append("unmapped-editor-range"); output.clean=false; }
    }
    auto report=resolutionReport(plan,output); report["editorModified"]=isModified(); return report;
}
} // namespace diffmerge::gui
