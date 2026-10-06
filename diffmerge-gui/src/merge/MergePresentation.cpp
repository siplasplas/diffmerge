#include "MergePresentation.h"
#include <diffmerge/MergePreviewWidget.h>
#include <diffmerge/DiffEditor.h>
#include <diffmerge/Comparison.h>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTimer>
#include <QtConcurrent>
#include <qce/CodeEditArea.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace diffmerge::gui {
namespace {
using Comparisons = std::array<std::shared_ptr<const PreparedComparison>,3>;
const std::array<MergeSource,3> sources{MergeSource::Ours,MergeSource::Theirs,MergeSource::Base};
class MergeConnectorHandle : public QSplitterHandle {
public:
    explicit MergeConnectorHandle(QSplitter* parent) : QSplitterHandle(Qt::Horizontal,parent) {}
    void install(DiffEditor* left,DiffEditor* right,bool reversed) {
        m_left = left; m_right = right; m_reversed = reversed;
        for (auto* editor : {left,right}) {
            connect(editor->edit()->area(),&qce::CodeEditArea::viewportChanged,this,[this] { update(); });
            connect(editor,&DiffEditor::colorSchemeChanged,this,[this] { update(); });
        }
    }
    void setComparison(std::shared_ptr<const PreparedComparison> comparison) { m_comparison = std::move(comparison); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.fillRect(rect(),palette().base());
        if (!m_comparison || !m_left || !m_right) return;
        auto* lv = m_left->edit()->area()->viewport(); auto* rv = m_right->edit()->area()->viewport();
        const auto left = m_left->edit()->area()->viewportState(), right = m_right->edit()->area()->viewportState();
        if (!left.isValid() || !right.isValid()) return;
        const int lo = mapFromGlobal(lv->mapToGlobal(QPoint(0,0))).y(), ro = mapFromGlobal(rv->mapToGlobal(QPoint(0,0))).y();
        const int top = std::max(lo,ro), bottom = std::min(lo+lv->height(),ro+rv->height());
        if (bottom <= top) return;
        painter.setClipRect(QRect(0,top,width(),bottom-top)); painter.setRenderHint(QPainter::Antialiasing);
        const auto y = [](int line,const qce::ViewportState& view,int origin) {
            return origin+view.contentOffsetY+qreal(line-view.firstVisibleLine)*view.lineHeight;
        };
        const qreal w = width(), mid = w/2;
        for (const auto& block : m_comparison->changes()) {
            const auto l = m_reversed ? block.rightRange : block.leftRange;
            const auto r = m_reversed ? block.leftRange : block.rightRange;
            const qreal lt=y(l.start,left,lo), lb=y(l.end(),left,lo), rt=y(r.start,right,ro), rb=y(r.end(),right,ro);
            if (std::max(lb,rb)<top || std::min(lt,rt)>=bottom) continue;
            QPainterPath upper; upper.moveTo(0,lt); upper.cubicTo(mid,lt,mid,rt,w,rt);
            QPainterPath lower; lower.moveTo(w,rb); lower.cubicTo(mid,rb,mid,lb,0,lb);
            auto fill=upper; fill.lineTo(w,rb); fill.connectPath(lower); fill.closeSubpath();
            const auto& scheme=m_left->colorScheme();
            painter.fillPath(fill,scheme.backgroundFor(block.type)); painter.setPen(scheme.stripeFor(block.type));
            painter.drawPath(upper); painter.drawPath(lower);
        }
    }
private:
    DiffEditor *m_left=nullptr,*m_right=nullptr;
    bool m_reversed=false;
    std::shared_ptr<const PreparedComparison> m_comparison;
};
class MergeConnectorSplitter : public QSplitter {
public:
    explicit MergeConnectorSplitter(QWidget* parent) : QSplitter(Qt::Horizontal,parent) {}
    void install(DiffEditor* ours,DiffEditor* result,DiffEditor* theirs) {
        static_cast<MergeConnectorHandle*>(handle(1))->install(ours,result,false);
        static_cast<MergeConnectorHandle*>(handle(2))->install(result,theirs,true);
    }
    void setComparisons(const Comparisons& comparisons) {
        static_cast<MergeConnectorHandle*>(handle(1))->setComparison(comparisons[0]);
        static_cast<MergeConnectorHandle*>(handle(2))->setComparison(comparisons[1]);
    }
protected:
    QSplitterHandle* createHandle() override { return new MergeConnectorHandle(this); }
};
struct Batch {
    quint64 revision=0;
    Comparisons comparisons;
    QString error;
};
QString documentText(DiffEditor* editor) {
    QStringList lines; auto* document=editor->edit()->area()->document();
    for (int i=0;i<document->lineCount();++i) lines.append(document->lineAt(i));
    return lines.join('\n');
}
}
struct MergePresentationState {
    MergePreviewWidget* owner;
    MergeConnectorSplitter* splitter;
    std::array<DiffEditor*,4> editors;
    std::array<qce::ViewportState,4> views;
    std::array<int,4> columns{};
    Comparisons comparisons;
    QTimer* timer;
    QFutureWatcher<Batch>* watcher;
    diffcore::CancellationToken cancellation;
    quint64 revision=0;
    bool pending=false,running=false,syncing=false,horizontal=false;
    int horizontalOffset=0;
};
QSplitter* createMergeSplitter(QWidget* parent) { return new MergeConnectorSplitter(parent); }
MergePresentation::MergePresentation(MergePreviewWidget* owner,QSplitter* splitter)
    : QObject(owner),m_state(std::make_unique<MergePresentationState>()) {
    auto& s=*m_state; s.owner=owner; s.splitter=static_cast<MergeConnectorSplitter*>(splitter);
    s.editors={owner->sourceEditor(MergeSource::Ours),owner->resultEditor(),owner->sourceEditor(MergeSource::Theirs),owner->sourceEditor(MergeSource::Base)};
    s.splitter->install(s.editors[0],s.editors[1],s.editors[2]);
    s.timer=new QTimer(this); s.timer->setSingleShot(true); s.timer->setInterval(100);
    s.watcher=new QFutureWatcher<Batch>(this);
    connect(s.timer,&QTimer::timeout,this,[this] { startUpdate(); });
    connect(s.watcher,&QFutureWatcher<Batch>::finished,this,[this] {
        auto& state=*m_state; state.running=false;
        const auto batch=state.watcher->result();
        if (batch.revision!=state.revision) { if (state.pending) state.timer->start(0); return; }
        state.pending=false; state.comparisons=batch.comparisons;
        state.splitter->setComparisons(state.owner->viewMode()==MergeViewMode::Conflicts ? Comparisons{} : state.comparisons);
        if (state.owner->viewMode()==MergeViewMode::Conflicts) {
            decorateConflicts();
            synchronizeVertical(1);
            if (!batch.error.isEmpty()) emit state.owner->operationFailed(batch.error);
            emit state.owner->comparisonsUpdated(); return;
        }
        auto* result=state.editors[1];
        QVector<diffcore::ChangeType> changes(result->edit()->area()->document()->lineCount(),diffcore::ChangeType::Equal);
        QVector<QVector<IntraLineDiffEngine::CharRange>> ranges(changes.size());
        for (int i=0;i<3;++i) {
            const auto& comparison=state.comparisons[i]; auto* source=state.editors[i==0 ? 0 : i==1 ? 2 : 3];
            if (!comparison) { source->setDocumentDiffs({},{}); continue; }
            source->setDocumentDiffs(comparison->model().docLineChanges(Side::Left),comparison->highlights().leftRanges);
            const auto& current=comparison->model().docLineChanges(Side::Right);
            const auto& highlights=comparison->highlights().rightRanges;
            for (int line=0;line<current.size() && line<changes.size();++line) {
                if (current[line]!=diffcore::ChangeType::Equal)
                    changes[line]=changes[line]==diffcore::ChangeType::Equal || changes[line]==current[line]
                        ? current[line] : diffcore::ChangeType::Replace;
                if (line<highlights.size()) ranges[line]+=highlights[line];
            }
        }
        for (auto& line : ranges) {
            std::sort(line.begin(),line.end(),[](const auto& a,const auto& b) { return a.start<b.start; });
            QVector<IntraLineDiffEngine::CharRange> merged;
            for (const auto& range : line) {
                if (!merged.isEmpty() && range.start<=merged.last().start+merged.last().length)
                    merged.last().length=std::max(merged.last().start+merged.last().length,range.start+range.length)-merged.last().start;
                else merged.append(range);
            }
            line=std::move(merged);
        }
        result->setDocumentDiffs(changes,ranges);
        if (!batch.error.isEmpty()) emit state.owner->operationFailed(batch.error);
        synchronizeVertical(1);
        emit state.owner->comparisonsUpdated();
    });
    connect(owner,&MergePreviewWidget::presentationChanged,this,[this] { decorateConflicts(); });
    for (int i=0;i<4;++i) {
        auto* area=s.editors[i]->edit()->area(); s.views[i]=area->viewportState();
        connect(area,&qce::CodeEditArea::viewportChanged,this,[this,i](const auto& view) {
            auto& state=*m_state; const auto previous=state.views[i]; state.views[i]=view;
            updateHorizontalRanges();
            if (view.firstVisibleLine!=previous.firstVisibleLine || view.contentOffsetY!=previous.contentOffsetY)
                synchronizeVertical(i);
        });
        connect(area->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this,i](int value) {
            auto& state=*m_state; if (state.horizontal) return;
            QScopedValueRollback<bool> guard(state.horizontal,true); state.horizontalOffset=value;
            for (int j=0;j<4;++j) if (j!=i) state.editors[j]->edit()->area()->horizontalScrollBar()->setValue(value);
        });
        auto* document=area->document();
        const auto changed=[this,i] {
            auto& state=*m_state; auto* doc=state.editors[i]->edit()->area()->document(); const int columns=doc->maxLineLength();
            state.columns[i]=columns; updateHorizontalRanges(); requestUpdate();
        };
        connect(document,&qce::ITextDocument::linesChanged,this,changed);
        connect(document,&qce::ITextDocument::linesInserted,this,changed);
        connect(document,&qce::ITextDocument::linesRemoved,this,changed);
        connect(document,&qce::ITextDocument::documentReset,this,changed);
    }
}
MergePresentation::~MergePresentation() { m_state->cancellation.requestCancellation(); }
bool MergePresentation::isUpdating() const { return m_state->pending || m_state->running; }
void MergePresentation::requestUpdate() {
    auto& s=*m_state; ++s.revision; s.pending=true; s.cancellation.requestCancellation();
    s.comparisons={}; s.splitter->setComparisons(s.comparisons);
    for (auto* editor:s.editors) editor->setDocumentDiffs({},{});
    decorateConflicts();
    s.timer->start(100);
}
void MergePresentation::decorateConflicts() {
    auto& s=*m_state;
    if (s.owner->viewMode()!=MergeViewMode::Conflicts) return;
    std::array<QVector<diffcore::ChangeType>,4> changes;
    for (int i=0;i<4;++i) changes[i].fill(diffcore::ChangeType::Equal,s.editors[i]->edit()->area()->document()->lineCount());
    for (const auto& conflict : s.owner->conflictPresentation()) {
        const std::array<std::optional<diffcore::LineRange>,4> ranges{conflict.ours,conflict.result,conflict.theirs,conflict.base};
        for (int i=0;i<4;++i) if (ranges[i] && conflict.state!=MergeResolutionState::Resolved)
            for (int line=std::max(0,ranges[i]->start);line<ranges[i]->end() && line<changes[i].size();++line)
                changes[i][line]=diffcore::ChangeType::Replace;
    }
    for (int i=0;i<4;++i) s.editors[i]->setDocumentDiffs(changes[i],{});
    s.splitter->setComparisons({});
}
void MergePresentation::startUpdate() {
    auto& s=*m_state; if (s.running) return;
    const auto session=s.owner->session(); const auto revision=s.revision;
    std::array<std::optional<TextSnapshot>,3> input;
    if (session && session->resultText()) for (int i=0;i<3;++i)
        if (i!=2 || s.owner->baseVisible()) input[i]=session->sourceText(sources[i]);
    const auto result=TextSnapshot::fromText(documentText(s.editors[1]));
    s.cancellation=diffcore::CancellationToken{}; const auto token=s.cancellation; s.running=true;
    s.watcher->setFuture(QtConcurrent::run([input,result,revision,token] {
        Batch batch; batch.revision=revision; ComparisonOptions options; options.diff.alignWhitespaceChanges=false;
        for (int i=0;i<3;++i) {
            if (!input[i]) continue;
            const auto prepared=prepareComparison(*input[i],result,options,token);
            if (prepared.status!=PreparationStatus::Ready) {
                batch.comparisons={};
                if (prepared.status!=PreparationStatus::Cancelled) batch.error=QStringLiteral("Merge comparison: %1").arg(prepared.message);
                return batch;
            }
            options.limits.maxWork-=prepared.workPerformed;
            batch.comparisons[i]=prepared.comparison;
        }
        return batch;
    }));
}
void MergePresentation::synchronizeVertical(int pane) {
    auto& s=*m_state; if (s.syncing || s.horizontal || !s.owner->session()) return;
    const int sourceIndex=pane==0 ? 0 : pane==2 ? 1 : 2;
    if (pane!=1 && !s.comparisons[sourceIndex]) return;
    QScopedValueRollback<bool> guard(s.syncing,true);
    const auto& view=s.views[pane]; const double threshold=0.4;
    const double anchor=view.firstVisibleLine+threshold*std::max(1,view.visibleLineCount());
    const double resultAnchor=pane==1 ? anchor : s.comparisons[sourceIndex]->scrollMapping().correspondingLine(Side::Left,anchor);
    for (int i=0;i<4;++i) {
        if (i==pane || (i==3 && !s.owner->baseVisible())) continue;
        const int index=i==0 ? 0 : i==2 ? 1 : 2;
        if (i!=1 && !s.comparisons[index]) continue;
        const double mapped=i==1 ? resultAnchor : s.comparisons[index]->scrollMapping().correspondingLine(Side::Right,resultAnchor);
        const int top=std::max(0,int(std::round(mapped-threshold*std::max(1,s.views[i].visibleLineCount()))));
        s.editors[i]->edit()->area()->verticalScrollBar()->setValue(top);
    }
}
void MergePresentation::updateHorizontalRanges() {
    auto& s=*m_state; if (s.horizontal) return;
    QScopedValueRollback<bool> guard(s.horizontal,true); int maximum=0;
    for (int i=0;i<4;++i) {
        if (i==3 && !s.owner->baseVisible()) continue;
        auto* area=s.editors[i]->edit()->area(); const int width=QFontMetrics(area->font()).horizontalAdvance(QLatin1Char('M'));
        maximum=std::max(maximum,s.columns[i]-(width>0 ? area->viewport()->width()/width : 0));
    }
    s.horizontalOffset=std::clamp(s.horizontalOffset,0,maximum);
    for (auto* editor:s.editors) {
        auto* bar=editor->edit()->area()->horizontalScrollBar();
        if (bar->maximum()!=maximum) { bar->setRange(0,maximum); bar->setValue(s.horizontalOffset); }
    }
}
} // namespace diffmerge::gui
