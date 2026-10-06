#include <diffmerge/MergePreviewWidget.h>
#include "MergePresentation.h"
#include <diffmerge/DiffEditor.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QSignalBlocker>
#include <QThread>
#include <QToolButton>
#include <qce/CodeEditArea.h>
#include <algorithm>
#include <stdexcept>

namespace diffmerge::gui {
namespace {
void display(DiffEditor* editor, const std::optional<TextSnapshot>& snapshot) {
    editor->setAlignedModel(nullptr);
    editor->setIntraLineDiffs({}); editor->setRevealOverlay(std::nullopt);
    auto lines = snapshot ? snapshot->lines : QStringList{};
    if (snapshot && snapshot->finalNewline.value_or(false)) lines.append(QString{});
    static_cast<qce::SimpleTextDocument*>(editor->edit()->area()->document())->setLines(lines);
    editor->setSyntaxFileName(snapshot ? snapshot->fileName : QString{});
    editor->edit()->area()->setReadOnly(true);
}
}
MergePreviewWidget::MergePreviewWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* navigation = new QHBoxLayout;
    m_previous = new QToolButton(this); m_previous->setText(QStringLiteral("Previous conflict"));
    m_next = new QToolButton(this); m_next->setText(QStringLiteral("Next conflict"));
    m_summary = new QLabel(this); m_summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_showBase = new QToolButton(this); m_showBase->setText(QStringLiteral("Show BASE")); m_showBase->setCheckable(true);
    m_showBase->setObjectName(QStringLiteral("showMergeBase"));
    navigation->addWidget(m_previous); navigation->addWidget(m_next); navigation->addWidget(m_summary); navigation->addWidget(m_showBase);
    layout->addLayout(navigation);
    m_splitter = createMergeSplitter(this); m_splitter->setChildrenCollapsible(false); m_splitter->setHandleWidth(24);
    const auto pane = [this](const QString& role, Side side, DiffEditor*& editor, QLabel*& label, QSplitter* splitter) {
        auto* widget = new QWidget(this); auto* row = new QVBoxLayout(widget); row->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(role, widget); label->setTextFormat(Qt::PlainText); label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        editor = new DiffEditor(side, widget);
        row->addWidget(label); row->addWidget(editor, 1);
        if (splitter) splitter->addWidget(widget);
        return widget;
    };
    pane(QStringLiteral("OURS"), Side::Left, m_ours, m_oursLabel, m_splitter);
    pane(QStringLiteral("RESULT"), Side::Right, m_result, m_resultLabel, m_splitter);
    pane(QStringLiteral("THEIRS"), Side::Right, m_theirs, m_theirsLabel, m_splitter);
    m_splitter->setSizes({1, 1, 1}); layout->addWidget(m_splitter, 3);
    m_basePane = pane(QStringLiteral("BASE"), Side::Left, m_base, m_baseLabel, nullptr);
    layout->addWidget(m_basePane, 1); m_basePane->hide();
    connect(m_showBase, &QToolButton::toggled, this, &MergePreviewWidget::setBaseVisible);
    connect(m_previous, &QToolButton::clicked, this, &MergePreviewWidget::navigateToPreviousConflict);
    connect(m_next, &QToolButton::clicked, this, &MergePreviewWidget::navigateToNextConflict);
    m_presentation = new MergePresentation(this,m_splitter);
    updateSummary();
}
bool MergePreviewWidget::setSession(std::shared_ptr<const PreparedMergeSession> session, const MarkerImportOptions& options) {
    Q_ASSERT(QThread::currentThread() == thread());
    MarkerImportResult imported; imported.status = MergeSessionStatus::Ready;
    if (session && session->resultText()) imported = importConflictMarkers(*session, options);
    if (imported.status != MergeSessionStatus::Ready) { emit operationFailed(imported.message); return false; }
    m_session = std::move(session); m_conflicts = std::move(imported.conflicts); m_current = -1;
    display(m_result, m_session ? m_session->resultText() : std::optional<TextSnapshot>{});
    const auto label = m_session && m_session->inputs().resultSeed ? m_session->inputs().resultSeed->file.label : QString{};
    m_resultLabel->setText(QStringLiteral("RESULT — %1").arg(m_session && m_session->resultText() ? label : QStringLiteral("unavailable")));
    m_resultLabel->setToolTip(m_resultLabel->text());
    m_sourcesInstalledFor.reset();
    updateSources(); updateSummary();
    m_presentation->requestUpdate();
    if (!m_conflicts.isEmpty()) navigateToConflict(0);
    else emit currentConflictChanged(-1);
    return true;
}
void MergePreviewWidget::updateSources() {
    for (auto source : {MergeSource::Base, MergeSource::Ours, MergeSource::Theirs}) {
        const auto role = source == MergeSource::Base ? QStringLiteral("BASE") : source == MergeSource::Ours ? QStringLiteral("OURS") : QStringLiteral("THEIRS");
        auto* label = source == MergeSource::Base ? m_baseLabel : source == MergeSource::Ours ? m_oursLabel : m_theirsLabel;
        std::optional<TextSnapshot> text;
        QString description = QStringLiteral("unavailable");
        if (m_session) {
            const auto& file = m_session->source(source);
            text = m_session->sourceText(source);
            description = text ? file.label : file.availability == MergeAvailability::Absent ? QStringLiteral("absent") : QStringLiteral("unknown");
            if (!text && m_current >= 0) {
                const auto& conflict = m_conflicts[m_current];
                std::optional<QByteArray> fragment;
                if (source == MergeSource::Base) fragment = conflict.base;
                else fragment = source == MergeSource::Ours ? conflict.ours : conflict.theirs;
                if (fragment) {
                    text = TextSnapshot::fromText(QString::fromUtf8(*fragment), {}, file.fileName);
                    description += QStringLiteral(" — conflict fragment only");
                }
            }
        }
        label->setText(role + QStringLiteral(" — ") + description); label->setToolTip(label->text());
        if (m_sourcesInstalledFor != m_session || !m_session || !m_session->sourceText(source))
            display(sourceEditor(source), text);
    }
    m_sourcesInstalledFor = m_session;
}
bool MergePreviewWidget::isComparisonUpdating() const { return m_presentation->isUpdating(); }
bool MergePreviewWidget::setViewMode(MergeViewMode mode) {
    if (mode != MergeViewMode::Conflicts && mode != MergeViewMode::SourceDifferences) return false;
    m_viewMode = mode; emit presentationChanged(); m_presentation->requestUpdate(); return true;
}
QVector<MergeConflictPresentation> MergePreviewWidget::conflictPresentation() const {
    QVector<MergeConflictPresentation> result;
    if (m_session && m_session->inputs().hostConflicts) {
        for (const auto& host : *m_session->inputs().hostConflicts)
            result.append({host.id,host.result,host.ours,host.theirs,host.base,host.state});
    } else for (const auto& marker : m_conflicts)
        result.append({marker.id,marker.resultLines,{},{},{},MergeResolutionState::Unresolved});
    return result;
}
bool MergePreviewWidget::canChooseSource(const QString&, MergeSource) const { return false; }
bool MergePreviewWidget::chooseSource(const QString&, MergeSource) { return false; }
void MergePreviewWidget::updateSummary() {
    QString text = m_current >= 0 ? QStringLiteral("Marker conflict %1 of %2").arg(m_current + 1).arg(m_conflicts.size())
                                 : QStringLiteral("No marker conflicts");
    if (m_session && m_session->inputs().hostConflicts) {
        int unresolved = 0;
        for (const auto& conflict : *m_session->inputs().hostConflicts)
            if (conflict.state != MergeResolutionState::Resolved) ++unresolved;
        text += QStringLiteral("; host reports %1 unresolved").arg(unresolved);
    }
    if (m_session && !m_session->inputs().hostConflicts) text += QStringLiteral("; host resolution state unavailable");
    m_summary->setText(text);
    m_previous->setEnabled(m_current > 0); m_next->setEnabled(m_current + 1 < m_conflicts.size());
}
bool MergePreviewWidget::navigateToConflict(int index) {
    if (index < 0 || index >= m_conflicts.size()) return false;
    m_current = index; updateSources();
    const auto range = m_conflicts[index].resultLines;
    m_result->edit()->area()->setCursorPosition({range.start, 0});
    m_result->setRevealOverlay(range);
    updateSummary(); emit currentConflictChanged(index); return true;
}
void MergePreviewWidget::navigateToNextConflict() { navigateToConflict(m_current + 1); }
void MergePreviewWidget::navigateToPreviousConflict() { navigateToConflict(m_current - 1); }
void MergePreviewWidget::setPanelSpacing(int pixels) { m_splitter->setHandleWidth(std::clamp(pixels, 8, 160)); }
int MergePreviewWidget::panelSpacing() const { return m_splitter->handleWidth(); }
void MergePreviewWidget::setBaseVisible(bool visible) {
    QSignalBlocker blocker(m_showBase); m_showBase->setChecked(visible); m_basePane->setVisible(visible);
    if (m_presentation) m_presentation->requestUpdate();
}
bool MergePreviewWidget::baseVisible() const { return !m_basePane->isHidden(); }
DiffEditor* MergePreviewWidget::sourceEditor(MergeSource source) const {
    switch (source) {
        case MergeSource::Base: return m_base;
        case MergeSource::Ours: return m_ours;
        case MergeSource::Theirs: return m_theirs;
    }
    throw std::invalid_argument("Invalid merge source");
}
} // namespace diffmerge::gui
