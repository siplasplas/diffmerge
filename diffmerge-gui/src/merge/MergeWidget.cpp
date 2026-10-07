#include <diffmerge/MergeWidget.h>
#include <diffmerge/DiffEditor.h>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QScopedValueRollback>
#include <QStringDecoder>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <qce/CodeEditArea.h>
#include <algorithm>
#include <functional>

namespace diffmerge::gui {
namespace {
std::optional<QByteArray> serialize(const QString& text, const QVector<LineEnding>& endings, bool bom, const MergeSessionLimits& limits) {
    if (std::uint64_t(text.size()) > limits.maxInputCodeUnits) return std::nullopt;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' || text[i].isNull() || text[i].isLowSurrogate()) return std::nullopt;
        if (text[i].isHighSurrogate() && (++i >= text.size() || !text[i].isLowSurrogate())) return std::nullopt;
    }
    const auto lines = text.split('\n');
    if (lines.size() - (text.endsWith('\n') ? 1 : 0) > qint64(limits.maxInputLines)) return std::nullopt;
    if (lines.size() - 1 != endings.size()) return std::nullopt;
    QByteArray bytes = bom ? QByteArray::fromHex("efbbbf") : QByteArray{};
    for (int i = 0; i < lines.size(); ++i) {
        if (std::uint64_t(lines[i].size()) > limits.maxLineCodeUnits) return std::nullopt;
        bytes += lines[i].toUtf8();
        if (i < endings.size()) switch (endings[i]) {
            case LineEnding::CRLF: bytes += "\r\n"; break;
            case LineEnding::CR: bytes += '\r'; break;
            case LineEnding::LF: bytes += '\n'; break;
            default: return std::nullopt;
        }
        if (std::uint64_t(bytes.size()) > limits.maxInputBytes) return std::nullopt;
    }
    return bytes;
}
struct Metadata {
    QVector<LineEnding> endings; // One entry per LF in normalized editor text.
    QVector<MergeEditableConflict> conflicts;
};
struct HistoryEntry { Metadata metadata; QByteArray hash; const QUndoCommand* command = nullptr; };
QString documentText(qce::CodeEditArea* area) {
    QStringList lines;
    for (int i = 0; i < area->document()->lineCount(); ++i) lines.append(area->document()->lineAt(i));
    return lines.join('\n');
}
QString normalized(const TextSnapshot& snapshot) {
    return snapshot.lines.join('\n') + (snapshot.finalNewline.value_or(false) ? QStringLiteral("\n") : QString{});
}
QVector<LineEnding> endings(const TextSnapshot& snapshot) {
    QVector<LineEnding> result;
    for (const auto ending : snapshot.lineEndings) if (ending != LineEnding::None) result.append(ending);
    return result;
}
int lineOffset(const QString& text, int line) {
    int offset = 0;
    for (int i = 0; i < line; ++i) {
        const int next = text.indexOf('\n', offset);
        if (next < 0) return text.size();
        offset = next + 1;
    }
    return offset;
}
bool boundary(const QString& text, int offset) {
    return offset >= 0 && offset <= text.size() && !(offset > 0 && offset < text.size()
        && text[offset].isLowSurrogate() && text[offset-1].isHighSurrogate());
}
bool validRange(const QString& text, MergeResultRange range) {
    return range.length >= 0 && range.start >= 0 && range.start <= text.size()
        && range.length <= text.size() - range.start && boundary(text, range.start) && boundary(text, range.start + range.length);
}
QByteArray hashText(const QString& text) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(text.constData()),text.size()*sizeof(QChar)), QCryptographicHash::Sha256);
}
bool same(const Metadata& a, const Metadata& b) {
    if (a.endings != b.endings || a.conflicts.size() != b.conflicts.size()) return false;
    for (int i = 0; i < a.conflicts.size(); ++i) {
        const auto& x = a.conflicts[i]; const auto& y = b.conflicts[i];
        if (x.id != y.id || x.range.start != y.range.start || x.range.length != y.range.length
            || x.mapped != y.mapped || x.state != y.state || x.choice != y.choice) return false;
    }
    return true;
}
TextSnapshot fragment(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    return TextSnapshot::fromText(QString(decoder(QByteArrayView(bytes))));
}
using SourceOffsets = std::array<QVector<qint64>,3>;
SourceOffsets sourceOffsets(const PreparedMergeSession& session) {
    SourceOffsets offsets;
    for (const auto source : {MergeSource::Base,MergeSource::Ours,MergeSource::Theirs}) {
        const int index = int(source);
        if (!session.sourceText(source)) continue;
        offsets[index].append(session.hasUtf8Bom(source) ? 3 : 0);
        const auto& snapshot = *session.sourceText(source);
        for (int line = 0; line < snapshot.lines.size(); ++line) {
            const auto ending = snapshot.lineEndings[line];
            offsets[index].append(offsets[index].last() + snapshot.lines[line].toUtf8().size()
                + (ending == LineEnding::CRLF ? 2 : ending == LineEnding::None ? 0 : 1));
        }
    }
    return offsets;
}
std::optional<QByteArray> sourceFragment(const PreparedMergeSession& session, MergeSource source,
    const std::optional<diffcore::LineRange>& range, const SourceOffsets& offsets) {
    if (!range || !session.sourceText(source)) return std::nullopt;
    const auto& positions = offsets[int(source)];
    const auto begin = positions[range->start], end = positions[range->end()];
    return session.source(source).bytes.mid(begin,end-begin);
}
// Replace a fragment and its metadata in one native Undo command. No document
// reset, stack clear or split text/state commands during ordinary edits.
class MergeCommand : public QUndoCommand {
public:
    MergeCommand(qce::CodeEditArea* area, int start, QString before, QString after,
        std::function<void(bool)> install, QString label)
        : m_area(area), m_start(start), m_before(std::move(before)), m_after(std::move(after)), m_install(std::move(install)) {
        setText(label); m_cursor = area->cursorPosition();
        m_anchor = m_cursor == area->selectionStart() ? area->selectionEnd() : area->selectionStart();
    }
    void redo() override { replace(m_before, m_after, false); }
    void undo() override { replace(m_after, m_before, true); }
private:
    qce::TextCursor position(int offset) const {
        const auto text = documentText(m_area); qce::TextCursor cursor{0,0};
        for (int i = 0; i < offset; ++i) { if (text[i] == '\n') { ++cursor.line; cursor.column = 0; } else ++cursor.column; }
        return cursor;
    }
    void replace(const QString& removed, const QString& inserted, bool undoing) {
        if (!removed.isEmpty()) m_area->document()->removeText(position(m_start), position(m_start + removed.size()));
        const auto cursor = inserted.isEmpty() ? position(m_start) : m_area->document()->insertText(position(m_start), inserted);
        m_install(undoing);
        if (undoing) m_area->setSelection(m_anchor, m_cursor); else m_area->setCursorPosition(cursor);
    }
    qce::CodeEditArea* m_area;
    int m_start;
    QString m_before, m_after;
    std::function<void(bool)> m_install;
    qce::TextCursor m_cursor, m_anchor;
};
}
struct MergeEditingState {
    Metadata current, clean;
    QVector<std::array<bool,3>> available;
    QVector<ImportedConflict> imported;
    QString text, cleanText;
    QVector<HistoryEntry> history;
    MarkerImportOptions options;
    MergeSessionLimits limits;
    bool editable = false, installing = false, discarding = false, forced = false, lastModified = false;
    int lastIndex = 0, lastUnresolved = 0;
    bool bom = false;
};
MergeWidget::MergeWidget(QWidget* parent) : MergePreviewWidget(parent), m_editing(std::make_unique<MergeEditingState>()) {
    qRegisterMetaType<MergeExportInput>(); qRegisterMetaType<MergeExportOptions>();
    m_actions = new QWidget(this); auto* row = new QHBoxLayout(m_actions);
    const QStringList labels{QStringLiteral("Take OURS"),QStringLiteral("Take THEIRS"),QStringLiteral("OURS then THEIRS"),
        QStringLiteral("THEIRS then OURS"),QStringLiteral("Take BASE"),QStringLiteral("Delete fragment")};
    const QVector<MergeChoice> choices{MergeChoice::Ours,MergeChoice::Theirs,MergeChoice::OursThenTheirs,
        MergeChoice::TheirsThenOurs,MergeChoice::Base,MergeChoice::Delete};
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QToolButton(m_actions); button->setText(labels[i]); row->addWidget(button); m_choices.append(button);
        connect(button,&QToolButton::clicked,this,[this,choice=choices[i]] { chooseConflict(m_current,choice); });
    }
    m_markResolved = new QToolButton(m_actions); m_markResolved->setText(QStringLiteral("Mark resolved")); row->addWidget(m_markResolved);
    m_markUnresolved = new QToolButton(m_actions); m_markUnresolved->setText(QStringLiteral("Mark unresolved")); row->addWidget(m_markUnresolved);
    connect(m_markResolved,&QToolButton::clicked,this,[this] { markConflictResolved(m_current); });
    connect(m_markUnresolved,&QToolButton::clicked,this,[this] { markConflictUnresolved(m_current); });
    auto* review = new QToolButton(m_actions); review->setText(QStringLiteral("Review selected range")); row->addWidget(review);
    connect(review,&QToolButton::clicked,this,[this] {
        auto* area = m_result->edit()->area();
        const auto start = area->selectionStart(), end = area->selectionEnd();
        const int begin = lineOffset(m_editing->text,start.line)+start.column;
        const int finish = lineOffset(m_editing->text,end.line)+end.column;
        reviewConflictRange(m_current,{begin,finish-begin});
    });
    layout()->addWidget(m_actions);
    auto* area = m_result->edit()->area();
    area->undoStack()->setUndoLimit(32);
    const auto edited = [this] { documentEdited(); };
    connect(area->document(),&qce::ITextDocument::linesChanged,this,edited);
    connect(area->document(),&qce::ITextDocument::linesInserted,this,edited);
    connect(area->document(),&qce::ITextDocument::linesRemoved,this,edited);
    connect(area->document(),&qce::ITextDocument::documentReset,this,edited);
    connect(area->undoStack(),&QUndoStack::indexChanged,this,&MergeWidget::undoIndexChanged);
    updateSummary();
}
MergeWidget::~MergeWidget() { m_editing->installing = true; m_result->edit()->area()->undoStack()->clear(); }
bool MergeWidget::setSession(std::shared_ptr<const PreparedMergeSession> session, const MarkerImportOptions& options) {
    if (isModified() && !m_editing->discarding) { emit operationFailed(QStringLiteral("Export or discard RESULT edits before replacing the session")); return false; }
    MarkerImportResult imported; imported.status = MergeSessionStatus::Ready;
    if (session && session->resultText()) imported = importConflictMarkers(*session,options);
    if (imported.status != MergeSessionStatus::Ready) { emit operationFailed(imported.message); return false; }
    if (imported.conflicts.size() > 4096 || (session && session->inputs().hostConflicts && session->inputs().hostConflicts->size() > 4096)) {
        emit operationFailed(QStringLiteral("Merge editor supports at most 4096 conflicts; use the read-only preview")); return false;
    }
    Metadata metadata;
    const auto offsets = session ? sourceOffsets(*session) : SourceOffsets{};
    QVector<std::array<bool,3>> available;
    const auto text = session && session->resultText() ? normalized(*session->resultText()) : QString{};
    if (session && session->resultText()) metadata.endings = endings(*session->resultText());
    auto fragments = imported.conflicts;
    QVector<ImportedConflict> ordered;
    if (session && session->inputs().hostConflicts) for (const auto& host : *session->inputs().hostConflicts) {
        const int match = [&] {
            if (!host.result) return -1;
            for (int i = 0; i < fragments.size(); ++i) if (fragments[i].resultLines.start == host.result->start && fragments[i].resultLines.count == host.result->count) return i;
            return -1;
        }();
        ImportedConflict item;
        if (match >= 0) {
            item = fragments.takeAt(match); item.id = host.id;
            if (auto bytes = sourceFragment(*session,MergeSource::Ours,host.ours,offsets)) item.ours = *bytes;
            if (auto bytes = sourceFragment(*session,MergeSource::Theirs,host.theirs,offsets)) item.theirs = *bytes;
            if (auto bytes = sourceFragment(*session,MergeSource::Base,host.base,offsets)) item.base = *bytes;
        }
        else {
            item.id = host.id;
            if (host.result) item.resultLines = *host.result;
            item.ours = sourceFragment(*session,MergeSource::Ours,host.ours,offsets).value_or(QByteArray{});
            item.theirs = sourceFragment(*session,MergeSource::Theirs,host.theirs,offsets).value_or(QByteArray{});
            item.base = sourceFragment(*session,MergeSource::Base,host.base,offsets);
        }
        MergeEditableConflict state; state.id = host.id; state.mapped = bool(host.result);
        if (host.result) { state.range.start = lineOffset(text,host.result->start); state.range.length = lineOffset(text,host.result->end()) - state.range.start; }
        state.state = match >= 0 && host.state == MergeResolutionState::Resolved ? MergeResolutionState::NeedsReview : host.state;
        metadata.conflicts.append(state);
        available.append({match >= 0 || (host.ours && session->sourceText(MergeSource::Ours)),
                          match >= 0 || (host.theirs && session->sourceText(MergeSource::Theirs)),item.base.has_value()});
        ordered.append(item);
    }
    QSet<QString> ids;
    for (const auto& state : metadata.conflicts) ids.insert(state.id);
    for (auto item : fragments) {
        while (ids.contains(item.id)) item.id.prepend(QStringLiteral("imported:"));
        ids.insert(item.id);
        MergeEditableConflict state; state.id = item.id; state.mapped = true;
        state.range.start = lineOffset(text,item.resultLines.start); state.range.length = lineOffset(text,item.resultLines.end()) - state.range.start;
        metadata.conflicts.append(state); available.append({true,true,item.base.has_value()}); ordered.append(item);
    }
    if (metadata.conflicts.size() > 4096) { emit operationFailed(QStringLiteral("Merge editor supports at most 4096 conflicts")); return false; }
    QScopedValueRollback<bool> installing(m_editing->installing,true);
    if (!MergePreviewWidget::setSession(session,options)) return false;
    m_conflicts = std::move(ordered); m_current = -1;
    m_editing->imported = imported.conflicts;
    m_editing->available = available;
    m_editing->current = metadata; m_editing->clean = metadata; m_editing->text = text; m_editing->cleanText = text;
    m_editing->options = options; m_editing->bom = session && session->resultHasUtf8Bom();
    auto* area = m_result->edit()->area(); area->undoStack()->clear();
    m_editing->history = {{metadata,hashText(text),nullptr}}; m_editing->lastIndex = 0;
    // Editing an existing empty file requires a cursor line, not an invented seed.
    if (session && session->resultText() && area->document()->lineCount() == 0)
        static_cast<qce::SimpleTextDocument*>(area->document())->setLines({QString{}});
    area->setReadOnly(!isEditable());
    m_editing->installing = false;
    if (!m_conflicts.isEmpty()) navigateToConflict(0);
    notifyState(); emit editableChanged(isEditable()); return true;
}
bool MergeWidget::setViewMode(MergeViewMode mode) {
    if (mode == viewMode()) return true;
    if (isModified()) { emit operationFailed(QStringLiteral("Export or discard edits before changing merge view mode")); return false; }
    return MergePreviewWidget::setViewMode(mode);
}
QVector<MergeConflictPresentation> MergeWidget::conflictPresentation() const {
    if (!m_editing) return {};
    QVector<MergeConflictPresentation> result;
    for (const auto& conflict : m_editing->current.conflicts) {
        MergeConflictPresentation item; item.id = conflict.id; item.state = conflict.state;
        if (conflict.mapped && validRange(m_editing->text,conflict.range)) {
            const int start = m_editing->text.left(conflict.range.start).count('\n');
            const int end = m_editing->text.left(conflict.range.start+conflict.range.length).count('\n');
            const bool partial = conflict.range.length>0 && m_editing->text[conflict.range.start+conflict.range.length-1]!='\n';
            item.result = diffcore::LineRange{start,end-start+(partial ? 1 : 0)};
        }
        if (m_session && m_session->inputs().hostConflicts) for (const auto& host : *m_session->inputs().hostConflicts)
            if (host.id == item.id) { item.ours = host.ours; item.theirs = host.theirs; item.base = host.base; break; }
        result.append(item);
    }
    return result;
}
bool MergeWidget::canChooseSource(const QString& id, MergeSource source) const {
    if (source != MergeSource::Ours && source != MergeSource::Theirs && source != MergeSource::Base) return false;
    for (int i=0;i<m_editing->current.conflicts.size();++i)
        if (m_editing->current.conflicts[i].id == id)
            return canChooseConflict(i,source == MergeSource::Ours ? MergeChoice::Ours : source == MergeSource::Theirs ? MergeChoice::Theirs : MergeChoice::Base);
    return false;
}
bool MergeWidget::chooseSource(const QString& id, MergeSource source) {
    if (!canChooseSource(id,source)) return false;
    for (int i=0;i<m_editing->current.conflicts.size();++i)
        if (m_editing->current.conflicts[i].id == id)
            return chooseConflict(i,source == MergeSource::Ours ? MergeChoice::Ours : source == MergeSource::Theirs ? MergeChoice::Theirs : MergeChoice::Base);
    return false;
}
void MergeWidget::setDecisionActionsVisible(bool visible) { m_actions->setVisible(visible); }
void MergeWidget::setEditable(bool editable) {
    if (editable && m_conflicts.size() > 4096) {
        emit operationFailed(QStringLiteral("Too many conflicts for editable Undo history (limit 4096)")); editable = false;
    }
    m_editing->editable = editable; m_result->edit()->area()->setReadOnly(!isEditable()); updateSummary(); emit editableChanged(isEditable()); emit presentationChanged(); }
bool MergeWidget::isWritable() const { return m_editing->editable && m_conflicts.size() <= 4096 && bool(m_session); }
bool MergeWidget::isEditable() const { return isWritable() && m_session->resultText().has_value(); }
bool MergeWidget::isModified() const { return m_editing->text != m_editing->cleanText || !same(m_editing->current,m_editing->clean); }
QString MergeWidget::resultText() const { return m_editing->text; }
QVector<MergeEditableConflict> MergeWidget::conflicts() const { return m_editing->current.conflicts; }
int MergeWidget::unresolvedCount() const {
    int count = 0; for (const auto& conflict : m_editing->current.conflicts) if (conflict.state != MergeResolutionState::Resolved) ++count; return count;
}
const QVector<ImportedConflict>& MergeWidget::markerConflicts() const { return m_editing->imported; }
void MergeWidget::navigateToNextUnresolvedConflict() {
    for (int i = m_current+1; i < m_editing->current.conflicts.size(); ++i)
        if (m_editing->current.conflicts[i].state != MergeResolutionState::Resolved) { navigateToConflict(i); return; }
}
void MergeWidget::navigateToPreviousUnresolvedConflict() {
    for (int i = m_current-1; i >= 0; --i)
        if (m_editing->current.conflicts[i].state != MergeResolutionState::Resolved) { navigateToConflict(i); return; }
}
std::optional<QByteArray> MergeWidget::resultBytes() const {
    if (!m_session || !m_session->resultText()) return std::nullopt;
    return serialize(m_editing->text,m_editing->current.endings,m_editing->bom,m_editing->limits);
}
std::optional<MergeExportInput> MergeWidget::captureExportInput() const {
    const auto bytes = resultBytes();
    if (!m_session || (m_session->resultText() && !bytes)) return std::nullopt;
    return MergeExportInput{m_session,bytes.value_or(QByteArray{}),m_editing->current.conflicts,m_editing->options,isWritable()};
}
bool MergeWidget::restoreDraft(const MergeExportInput& captured) {
    if (isModified()) { emit operationFailed(QStringLiteral("Export or discard RESULT edits before restoring a draft")); return false; }
    MergeExportOptions options; options.draftFormat = MergeDraftFormat::HostBuffer;
    const auto checked = prepareMergeExport(captured,options,m_editing->limits);
    if (checked.status != MergeSessionStatus::Ready) { emit operationFailed(checked.message); return false; }
    // Validate the identities generated by the original seed before mutating this widget.
    const auto imported = importConflictMarkers(*captured.session,captured.markerOptions);
    if (imported.status != MergeSessionStatus::Ready) { emit operationFailed(imported.message); return false; }
    auto fragments = imported.conflicts;
    QStringList expected;
    if (captured.session->inputs().hostConflicts) for (const auto& host : *captured.session->inputs().hostConflicts) {
        expected.append(host.id);
        if (host.result) for (int i = 0; i < fragments.size(); ++i)
            if (fragments[i].resultLines.start == host.result->start && fragments[i].resultLines.count == host.result->count) {
                fragments.removeAt(i); break;
            }
    }
    for (auto fragment : fragments) {
        while (expected.contains(fragment.id)) fragment.id.prepend(QStringLiteral("imported:"));
        expected.append(fragment.id);
    }
    if (expected.size() != captured.conflicts.size() || expected.size() > 4096) {
        emit operationFailed(QStringLiteral("Draft conflict identities differ from the original session")); return false;
    }
    QVector<MergeEditableConflict> restored;
    for (const auto& id : expected) {
        const auto found = std::find_if(captured.conflicts.begin(),captured.conflicts.end(),[&](const auto& conflict) { return conflict.id == id; });
        if (found == captured.conflicts.end()) { emit operationFailed(QStringLiteral("Draft conflict identities differ from the original session")); return false; }
        restored.append(*found);
    }
    MergeSessionInputs current; MergeResultSeed seed;
    seed.file.availability = MergeAvailability::Present; seed.file.bytes = captured.resultBytes;
    current.resultSeed = seed;
    const auto decoded = prepareMergeSession(current,m_editing->limits);
    if (decoded.status != MergeSessionStatus::Ready) { emit operationFailed(decoded.message); return false; }
    if (!setSession(captured.session,captured.markerOptions)) return false;
    {
        QScopedValueRollback<bool> installing(m_editing->installing,true);
        const auto& snapshot = *decoded.session->resultText();
        m_editing->text = normalized(snapshot); m_editing->cleanText = m_editing->text;
        m_editing->current = {endings(snapshot),restored}; m_editing->clean = m_editing->current;
        m_editing->bom = decoded.session->resultHasUtf8Bom();
        auto lines = m_editing->text.split('\n');
        auto* area = m_result->edit()->area();
        static_cast<qce::SimpleTextDocument*>(area->document())->setLines(lines);
        area->undoStack()->clear(); m_editing->lastIndex = 0; m_editing->forced = false;
        m_editing->history = {{m_editing->current,hashText(m_editing->text),nullptr}};
    }
    if (!m_conflicts.isEmpty()) navigateToConflict(0);
    notifyState(); return true;
}
PrepareMergeExportResult MergeWidget::exportResult(const MergeExportOptions& options,
    const MergeSessionLimits& limits, const diffcore::CancellationToken& cancellation) const {
    if (options.disposition == MergeExportDisposition::Cancelled)
        return prepareMergeExport({},options,limits,cancellation);
    const auto input = captureExportInput();
    if (!input) return {MergeSessionStatus::Error,std::nullopt,QStringLiteral("RESULT cannot be serialized losslessly")};
    return prepareMergeExport(*input,options,limits,cancellation);
}
bool MergeWidget::requestSave(const MergeExportOptions& options) {
    const auto captured = captureExportInput();
    if (!isWritable() || !captured || options.disposition == MergeExportDisposition::Cancelled) {
        emit operationFailed(QStringLiteral("A save request requires a writable, serializable RESULT")); return false;
    }
    emit saveRequested(*captured,options); return true;
}
bool MergeWidget::requestExport(const MergeExportOptions& options) {
    const auto captured = captureExportInput();
    if (!captured || (options.disposition == MergeExportDisposition::Resolved && !isWritable())) {
        emit operationFailed(QStringLiteral("This RESULT cannot be exported in the requested mode")); return false;
    }
    emit exportRequested(*captured,options); return true;
}
bool MergeWidget::requestFinish(const MergeExportOptions& options) {
    const auto captured = captureExportInput();
    if (!isWritable() || !captured || options.disposition != MergeExportDisposition::Resolved) {
        emit operationFailed(QStringLiteral("Finish requires a writable RESULT and an explicit Resolved request")); return false;
    }
    emit finishRequested(*captured,options); return true;
}
bool MergeWidget::acknowledgeSaved(const MergeExportInput& captured) {
    const auto bytes = resultBytes();
    const Metadata saved{m_editing->current.endings,captured.conflicts};
    if (captured.session != m_session || !bytes || *bytes != captured.resultBytes || !same(saved,m_editing->current)) return false;
    m_editing->clean = m_editing->current; m_editing->cleanText = m_editing->text;
    notifyState(); return true;
}
void MergeWidget::documentEdited() {
    if (m_editing->installing) return;
    const auto next = documentText(m_result->edit()->area()); const auto before = m_editing->text;
    int prefix = 0, suffix = 0;
    while (prefix < before.size() && prefix < next.size() && before[prefix] == next[prefix]) ++prefix;
    while (suffix < before.size()-prefix && suffix < next.size()-prefix && before[before.size()-1-suffix] == next[next.size()-1-suffix]) ++suffix;
    const int end = before.size()-suffix, inserted = next.size()-suffix-prefix, delta = inserted - (end-prefix);
    const int leading = before.left(prefix).count('\n'), removed = before.mid(prefix,end-prefix).count('\n');
    auto& endings = m_editing->current.endings;
    const auto defaultEnding = leading > 0 && leading <= endings.size() ? endings[leading-1] : endings.isEmpty() ? LineEnding::LF : endings.first();
    endings.remove(leading,removed);
    const int added = next.mid(prefix,inserted).count('\n');
    for (int i = 0; i < added; ++i) endings.insert(leading+i,defaultEnding);
    for (auto& conflict : m_editing->current.conflicts) {
        if (!conflict.mapped) continue;
        const int start = conflict.range.start, finish = start + conflict.range.length;
        if (end <= start) conflict.range.start += delta;
        else if (prefix < finish) {
            conflict.state = MergeResolutionState::NeedsReview; conflict.choice = MergeChoice::Manual;
            if (prefix < start || end >= finish) conflict.mapped = false;
            const int newStart = std::min(start,prefix), newEnd = finish > end ? finish+delta : prefix+inserted;
            conflict.range = {newStart,std::max(0,newEnd-newStart)};
        }
    }
    m_editing->text = next;
    // Native commands can emit multiple document signals; publish when their
    // Undo index changes, then restore the exact sidecar state for Undo/Redo.
}
void MergeWidget::undoIndexChanged(int index) {
    if (m_editing->installing) return;
    const auto* stack = m_result->edit()->area()->undoStack();
    const auto* command = index ? stack->command(index-1) : nullptr;
    const auto hash = hashText(m_editing->text);
    auto& history = m_editing->history;
    // QUndoStack drops its oldest command at the configured limit. Rebase the
    // sidecar on the surviving first command, including its preceding state.
    if (stack->count() > 0) {
        const auto* first = stack->command(0);
        for (int i = 1; i < history.size(); ++i) if (history[i].command == first) {
            if (i > 1) history.remove(0,i-1);
            break;
        }
    }
    const bool matches = index < history.size() && history[index].command == command && history[index].hash == hash;
    if (matches && !m_editing->forced) m_editing->current = history[index].metadata;
    else {
        if (index >= m_editing->lastIndex && !matches) history.resize(index+1);
        if (index >= history.size()) history.resize(index+1);
        history[index] = {m_editing->current,hash,command};
    }
    m_editing->forced = false; m_editing->lastIndex = index; notifyState();
}
bool MergeWidget::canChooseConflict(int index, MergeChoice choice) const {
    if (!isEditable() || index < 0 || index >= m_conflicts.size() || !m_editing->current.conflicts[index].mapped) return false;
    const auto& available = m_editing->available[index];
    switch (choice) {
        case MergeChoice::Ours: return available[0];
        case MergeChoice::Theirs: return available[1];
        case MergeChoice::OursThenTheirs: case MergeChoice::TheirsThenOurs: return available[0] && available[1];
        case MergeChoice::Base: return available[2];
        case MergeChoice::Delete: return true;
        default: return false;
    }
}
bool MergeWidget::chooseConflict(int index, MergeChoice choice) {
    if (!canChooseConflict(index,choice)) return false;
    const auto& conflict = m_editing->current.conflicts[index];
    if (!conflict.mapped || !validRange(m_editing->text,conflict.range) || !resultBytes()) { emit operationFailed(QStringLiteral("Review the RESULT range before replacing this conflict")); return false; }
    for (int i = 0; i < m_editing->current.conflicts.size(); ++i) if (i != index) {
        const auto& other = m_editing->current.conflicts[i];
        if (other.mapped && std::max(other.range.start,conflict.range.start) < std::min(other.range.start+other.range.length,conflict.range.start+conflict.range.length))
            { emit operationFailed(QStringLiteral("Conflict ranges overlap; explicit review is required")); return false; }
    }
    const auto& fragments = m_conflicts[index]; QByteArray bytes;
    switch (choice) {
        case MergeChoice::Ours: bytes = fragments.ours; break;
        case MergeChoice::Theirs: bytes = fragments.theirs; break;
        case MergeChoice::OursThenTheirs: bytes = fragments.ours + fragments.theirs; break;
        case MergeChoice::TheirsThenOurs: bytes = fragments.theirs + fragments.ours; break;
        case MergeChoice::Base: if (!fragments.base) return false; bytes = *fragments.base; break;
        case MergeChoice::Delete: break;
        default: return false;
    }
    return replaceConflictFragment(index, bytes, choice, true);
}
bool MergeWidget::replaceConflictText(int index, const QByteArray& bytes, bool resolved) {
    return replaceConflictFragment(index, bytes, MergeChoice::Manual, resolved);
}
bool MergeWidget::replaceConflictFragment(int index, const QByteArray& bytes, MergeChoice choice, bool resolved) {
    if (!canChooseConflict(index, MergeChoice::Delete)) return false;
    const auto& conflict = m_editing->current.conflicts[index];
    if (!validRange(m_editing->text, conflict.range) || !resultBytes()) return false;
    for (int i = 0; i < m_editing->current.conflicts.size(); ++i) if (i != index) {
        const auto& other = m_editing->current.conflicts[i];
        if (other.mapped && std::max(other.range.start,conflict.range.start) <
            std::min(other.range.start+other.range.length,conflict.range.start+conflict.range.length)) return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder(bytes);
    if (decoder.hasError() || decoded.contains(QChar::Null)) {
        emit operationFailed(QStringLiteral("Conflict replacement must be valid UTF-8 text")); return false;
    }
    const auto replacement = fragment(bytes); const auto afterText = normalized(replacement);
    const auto before = m_editing->current; auto after = before;
    const auto range = conflict.range; const int delta = afterText.size() - range.length;
    const int leading = m_editing->text.left(range.start).count('\n'), removed = m_editing->text.mid(range.start,range.length).count('\n');
    after.endings.remove(leading,removed); const auto inserted = endings(replacement);
    for (int i = 0; i < inserted.size(); ++i) after.endings.insert(leading+i,inserted[i]);
    for (int i = 0; i < after.conflicts.size(); ++i) {
        auto& state = after.conflicts[i];
        if (i == index) { state.range.length = afterText.size(); state.state = resolved ? MergeResolutionState::Resolved : MergeResolutionState::NeedsReview; state.choice = choice; }
        else if (state.mapped && state.range.start >= range.start+range.length) state.range.start += delta;
    }
    const auto projected = m_editing->text.left(range.start) + afterText + m_editing->text.mid(range.start+range.length);
    if (!serialize(projected,after.endings,m_editing->bom,m_editing->limits)) {
        emit operationFailed(QStringLiteral("Conflict choice exceeds text limits or cannot be serialized losslessly")); return false;
    }
    const auto install = [this,before,after](bool undoing) { m_editing->current = undoing ? before : after; m_editing->forced = true; };
    m_result->edit()->area()->undoStack()->push(new MergeCommand(m_result->edit()->area(),range.start,
        m_editing->text.mid(range.start,range.length),afterText,install,QStringLiteral("Choose conflict fragment")));
    return true;
}
bool MergeWidget::changeState(int index, MergeResolutionState state, MergeChoice choice) {
    if (!isEditable() || index < 0 || index >= m_conflicts.size() || !m_editing->current.conflicts[index].mapped || !resultBytes()) return false;
    const auto before = m_editing->current; auto after = before;
    after.conflicts[index].state = state; after.conflicts[index].choice = choice;
    const auto install = [this,before,after](bool undoing) { m_editing->current = undoing ? before : after; m_editing->forced = true; };
    m_result->edit()->area()->undoStack()->push(new MergeCommand(m_result->edit()->area(),0,{}, {},install,QStringLiteral("Change conflict resolution state")));
    return true;
}
bool MergeWidget::markConflictResolved(int index) { return changeState(index,MergeResolutionState::Resolved,MergeChoice::Manual); }
bool MergeWidget::markConflictUnresolved(int index) { return changeState(index,MergeResolutionState::Unresolved,MergeChoice::Unresolved); }
bool MergeWidget::reviewConflictRange(int index, MergeResultRange range) {
    if (!isEditable() || index < 0 || index >= m_conflicts.size() || !validRange(m_editing->text,range)) return false;
    const auto before = m_editing->current; auto after = before;
    auto& conflict = after.conflicts[index]; conflict.range = range; conflict.mapped = true;
    conflict.state = MergeResolutionState::NeedsReview; conflict.choice = MergeChoice::Manual;
    const auto install = [this,before,after](bool undoing) { m_editing->current = undoing ? before : after; m_editing->forced = true; };
    m_result->edit()->area()->undoStack()->push(new MergeCommand(m_result->edit()->area(),0,{}, {},install,QStringLiteral("Review conflict result range")));
    return true;
}
void MergeWidget::discardChanges() {
    const auto session = m_session; const auto options = m_editing->options;
    QScopedValueRollback<bool> discarding(m_editing->discarding,true);
    setSession(session,options);
}
bool MergeWidget::navigateToConflict(int index) {
    if (!m_editing || m_editing->installing || index < 0 || index >= m_conflicts.size()) return false;
    m_current = index; updateSources();
    const auto& state = m_editing->current.conflicts[index];
    if (state.mapped) {
        const auto prefix = m_editing->text.left(state.range.start); const int line = prefix.count('\n');
        const int lastNewline = prefix.lastIndexOf('\n');
        m_result->edit()->area()->setCursorPosition({line,state.range.start-lastNewline-1});
        m_result->setRevealOverlay(diffcore::LineRange{line,std::max(1,int(m_editing->text.mid(state.range.start,state.range.length).count('\n')))});
    } else m_result->setRevealOverlay(std::nullopt);
    updateSummary(); emit currentConflictChanged(index); return true;
}
void MergeWidget::notifyState() {
    if (m_editing->lastModified != isModified()) { m_editing->lastModified = isModified(); emit modifiedChanged(m_editing->lastModified); }
    const int unresolved = unresolvedCount();
    if (m_editing->lastUnresolved != unresolved) { m_editing->lastUnresolved = unresolved; emit unresolvedCountChanged(unresolved); }
    updateSummary(); emit conflictStatesChanged(); emit presentationChanged();
}
void MergeWidget::updateSummary() {
    if (!m_editing) { MergePreviewWidget::updateSummary(); return; }
    m_summary->setText(QStringLiteral("%1 conflicts; %2 unresolved%3").arg(m_conflicts.size()).arg(unresolvedCount()).arg(isModified() ? QStringLiteral("; modified") : QString{}));
    if (m_session && !m_session->inputs().hostConflicts) m_summary->setText(m_summary->text()+QStringLiteral("; host resolution state unavailable"));
    m_previous->setEnabled(m_current > 0); m_next->setEnabled(m_current+1 < m_conflicts.size());
    if (!m_actions) return;
    const bool selected = isEditable() && m_current >= 0 && m_current < m_editing->current.conflicts.size();
    const bool mapped = selected && m_editing->current.conflicts[m_current].mapped;
    const QVector<MergeChoice> choices{MergeChoice::Ours,MergeChoice::Theirs,MergeChoice::OursThenTheirs,
        MergeChoice::TheirsThenOurs,MergeChoice::Base,MergeChoice::Delete};
    for (int i = 0; i < m_choices.size(); ++i) m_choices[i]->setEnabled(canChooseConflict(m_current,choices[i]));
    m_markResolved->setEnabled(mapped); m_markUnresolved->setEnabled(mapped);
}
} // namespace diffmerge::gui
