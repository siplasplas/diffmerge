#include <diffmerge/FileDiffWidget.h>
#include <diffmerge/DiffEditor.h>
#include "FileEditingState.h"
#include "DiffConnectorSplitter.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStringConverter>
#include <QUndoCommand>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>
#include <qce/CodeEditArea.h>

namespace diffmerge::gui {
namespace {
int index(Side side) { return side == Side::Left ? 0 : 1; }
QString normalized(const TextSnapshot& snapshot) {
    return snapshot.lines.join('\n') + (snapshot.finalNewline.value_or(false) ? QStringLiteral("\n") : QString{});
}
QString documentText(const DiffEditor* editor) {
    QStringList lines;
    const auto* doc = editor->edit()->area()->document();
    for (int i = 0; i < doc->lineCount(); ++i) lines.append(doc->lineAt(i));
    return lines.join('\n');
}
QByteArray fingerprint(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result();
}
bool writable(const QFileInfo& file) {
    return !file.exists() || (file.isFile() && file.isWritable() &&
        bool(file.permissions() & (QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther)));
}
// One command owns the replacement and retains the editor's native undo stack.
class ReplaceText : public QUndoCommand {
public:
    ReplaceText(qce::CodeEditArea* area, QString before, QString after, bool& syncing)
        : m_area(area), m_cursor(area->cursorPosition()), m_syncing(syncing) {
        m_anchor = m_cursor == area->selectionStart() ? area->selectionEnd() : area->selectionStart();
        int prefix=0, suffix=0;
        while (prefix<before.size() && prefix<after.size() && before[prefix]==after[prefix]) ++prefix;
        if (prefix>0 && prefix<before.size() && before[prefix].isLowSurrogate() && before[prefix-1].isHighSurrogate()) --prefix;
        while (suffix<before.size()-prefix && suffix<after.size()-prefix && before[before.size()-1-suffix]==after[after.size()-1-suffix]) ++suffix;
        if (suffix>0 && suffix<before.size() && before[before.size()-suffix].isLowSurrogate() && before[before.size()-suffix-1].isHighSurrogate()) --suffix;
        const auto position = [](const QString& text,int offset) {
            qce::TextCursor result{0,0};
            for (int n=0; n<offset; ++n) { if (text[n]=='\n') { ++result.line; result.column=0; } else ++result.column; }
            return result;
        };
        m_start=position(before,prefix); m_endBefore=position(before,int(before.size())-suffix);
        m_before=before.mid(prefix,before.size()-prefix-suffix); m_after=after.mid(prefix,after.size()-prefix-suffix);
    }
    void undo() override { replace(m_endAfter,m_before,true); }
    void redo() override { m_endAfter=replace(m_endBefore,m_after,false); }
private:
    qce::TextCursor replace(qce::TextCursor end, const QString& text, bool undoing) {
        QScopedValueRollback<bool> syncing(m_syncing,true);
        const int vertical = m_area->verticalScrollBar()->value();
        const int horizontal = m_area->horizontalScrollBar()->value();
        auto* doc = m_area->document();
        doc->removeText(m_start,end);
        const auto cursor = doc->insertText(m_start,text);
        if (undoing) m_area->setSelection(m_anchor,m_cursor);
        else m_area->setCursorPosition(cursor);
        m_area->verticalScrollBar()->setValue(vertical);
        m_area->horizontalScrollBar()->setValue(horizontal);
        return cursor;
    }
    qce::CodeEditArea* m_area;
    QString m_before, m_after;
    qce::TextCursor m_cursor;
    qce::TextCursor m_anchor;
    bool& m_syncing;
    qce::TextCursor m_start{}, m_endBefore{}, m_endAfter{};
};
}

void FileDiffWidget::setupEditing() {
    m_editing = std::make_unique<FileEditingState>();
    m_editing->timer = new QTimer(this);
    m_editing->timer->setSingleShot(true); m_editing->timer->setInterval(300);
    connect(m_editing->timer, &QTimer::timeout, this, &FileDiffWidget::recomputeEditedComparison);
    for (Side side : {Side::Left, Side::Right}) {
        auto* doc = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area()->document();
        const auto edited = [this, side] { documentEdited(side); };
        connect(doc, &qce::ITextDocument::linesChanged, this, edited);
        connect(doc, &qce::ITextDocument::linesInserted, this, edited);
        connect(doc, &qce::ITextDocument::linesRemoved, this, edited);
        connect(doc, &qce::ITextDocument::documentReset, this, edited);
    }
}
bool FileDiffWidget::isEditable(Side side) const { return m_editing->editable[index(side)] && !m_editing->unsafe[index(side)] && !m_editing->encodingUnsafe[index(side)]; }
bool FileDiffWidget::isModified(Side side) const { return m_editing->modified[index(side)]; }
bool FileDiffWidget::isRecomputing() const { return m_editing->pending; }
void FileDiffWidget::setDiffOptions(const diffcore::DiffOptions& options) {
    m_options.diff = options;
    if (!m_comparison || m_binaryInput) return;
    // Invalidate old ranges immediately; debounce keeps rapid toggles inexpensive.
    ++m_editing->generation; m_editing->cancellation.requestCancellation();
    m_editing->pending = true; m_editing->timer->start();
    m_model = nullptr; clearSearchHighlights(); m_splitter->setModel(nullptr);
    m_currentHunk = -1; updateNavLabel();
}
QString FileDiffWidget::text(Side side) const {
    if (!m_comparison) return {};
    return m_editing->raw[index(side)] ? documentText(side == Side::Left ? m_leftEditor : m_rightEditor)
                                      : normalized(m_comparison->snapshot(side));
}
void FileDiffWidget::setEditable(Side side, bool editable) {
    if (!editable && isModified(side)) {
        emit operationFailed(QStringLiteral("Save or discard edits before locking the side"));
        emit editableChanged(side, isEditable(side)); return;
    }
    m_editing->editable[index(side)] = editable;
    updateEditability(); emit editableChanged(side, isEditable(side));
}
void FileDiffWidget::resetEditing() {
    ++m_editing->generation; m_editing->cancellation.requestCancellation();
    m_editing->timer->stop(); m_editing->pending = false;
    for (Side side : {Side::Left, Side::Right}) {
        const int i = index(side);
        if (m_editing->modified[i]) { m_editing->modified[i] = false; emit modifiedChanged(side, false); }
        m_editing->raw[i] = false; m_editing->unsafe[i] = false; m_editing->bom[i] = false;
        m_editing->encodingUnsafe[i] = false;
        m_editing->targets[i].clear(); m_editing->canonicalTargets[i].clear(); m_editing->diskHash[i].clear();
        m_editing->cleanSnapshots[i] = m_comparison ? m_comparison->snapshot(side) : TextSnapshot{};
        m_editing->cleanText[i] = normalized(m_editing->cleanSnapshots[i]);
    }
    updateEditability();
}
void FileDiffWidget::updateEditability() {
    QScopedValueRollback<bool> installing(m_editing->installing, true);
    const bool projected = m_viewMode != ViewMode::SideBySide || m_skipUnchanged;
    for (Side side : {Side::Left, Side::Right}) {
        const int i = index(side);
        auto* editor = side == Side::Left ? m_leftEditor : m_rightEditor;
        const bool enabled = m_comparison && isEditable(side) && !projected;
        // Include the final empty cursor line: Enter at EOF must be an actual edit.
        if (enabled && !m_editing->raw[i]) {
            const auto lines = normalized(m_comparison->snapshot(side)).split('\n');
            static_cast<qce::SimpleTextDocument*>(editor->edit()->area()->document())->setLines(lines);
            m_editing->raw[i] = true;
        }
        editor->edit()->area()->setReadOnly(!enabled);
        auto* lock = side == Side::Left ? m_leftLock : m_rightLock;
        auto* saveButton = side == Side::Left ? m_leftSave : m_rightSave;
        saveButton->setVisible(m_editing->modified[i]);
        saveButton->setEnabled(isEditable(side) && m_editing->modified[i]);
        lock->setText(enabled ? QStringLiteral("✎") : QStringLiteral("🔒"));
        if (m_editing->modified[i]) lock->setText(lock->text()+'*');
        lock->setToolTip(m_editing->encodingUnsafe[i] ? QStringLiteral("Not UTF-8: read-only") :
            (enabled ? QStringLiteral("Editable side") : QStringLiteral("Read-only side")));
    }
    m_unifiedEditor->edit()->area()->setReadOnly(true);
    m_editHint->setVisible(projected && (isEditable(Side::Left) || isEditable(Side::Right)));
    m_splitter->setCopyActions(!projected && isEditable(Side::Left) && !m_editing->encodingUnsafe[1],
        !projected && isEditable(Side::Right) && !m_editing->encodingUnsafe[0],
        [this](int block, Side source) { copyChange(block, source); });
}
void FileDiffWidget::documentEdited(Side side) {
    if (m_editing->installing || !m_editing->raw[index(side)]) return;
    const int i = index(side);
    const bool modified = text(side) != m_editing->cleanText[i];
    if (modified != m_editing->modified[i]) { m_editing->modified[i] = modified; emit modifiedChanged(side, modified); }
    ++m_editing->generation; m_editing->cancellation.requestCancellation();
    m_editing->pending = true; m_editing->timer->start();
    m_model = nullptr; clearSearchHighlights();
    m_currentHunk = -1; updateNavLabel();
    (side == Side::Left ? m_leftLock : m_rightLock)->setText(modified ? QStringLiteral("✎*") : QStringLiteral("✎"));
    auto* saveButton = side == Side::Left ? m_leftSave : m_rightSave;
    saveButton->setVisible(modified); saveButton->setEnabled(isEditable(side) && modified);
    // Old coordinates are unsafe until the new result arrives.
    m_splitter->setModel(nullptr);
    for (auto* editor : {m_leftEditor, m_rightEditor}) {
        editor->setAlignedModel(nullptr, true); editor->setIntraLineDiffs({});
    }
}
void FileDiffWidget::recomputeEditedComparison() {
    if (!m_comparison) return;
    const auto make = [&](Side side) {
        const auto& old = m_comparison->snapshot(side);
        return m_editing->raw[index(side)] ? TextSnapshot::fromText(text(side), old.label, old.fileName) : old;
    };
    auto left = make(Side::Left), right = make(Side::Right);
    const auto options = m_options;
    const auto generation = m_editing->generation;
    m_editing->cancellation = {}; const auto token = m_editing->cancellation;
    auto* watcher = new QFutureWatcher<PrepareResult>(this);
    connect(watcher, &QFutureWatcher<PrepareResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_editing->generation) return;
        m_editing->pending = false;
        if (result.status == PreparationStatus::Ready) installEditedComparison(result.comparison);
        else emit operationFailed(result.message);
    });
    watcher->setFuture(QtConcurrent::run([left, right, options, token] { return prepareComparison(left, right, options, token); }));
}
void FileDiffWidget::installEditedComparison(std::shared_ptr<const PreparedComparison> comparison) {
    const auto previous = std::move(m_comparison);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    m_comparison = std::move(comparison); m_model = &m_comparison->model();
    const auto threshold = m_syncMapper.threshold(); m_syncMapper = m_comparison->scrollMapping(); m_syncMapper.setThreshold(threshold);
    if(!m_skipUnchanged) {
        m_leftEditor->setAlignedModel(m_model, true); m_rightEditor->setAlignedModel(m_model, true);
        m_leftEditor->setIntraLineDiffs(m_comparison->highlights().leftRanges);
        m_rightEditor->setIntraLineDiffs(m_comparison->highlights().rightRanges);
    }
    m_splitter->setModel(m_model); m_currentHunk = -1; updateNavLabel();
    if(m_viewMode != ViewMode::SideBySide || m_skipUnchanged) rebuildProjection();
    emit comparisonChanged(changeCount());
}
void FileDiffWidget::discardChanges() {
    if (!isModified(Side::Left) && !isModified(Side::Right)) return;
    auto prepared = prepareComparison(m_editing->cleanSnapshots[0], m_editing->cleanSnapshots[1], m_options);
    if (prepared.status != PreparationStatus::Ready) { emit operationFailed(prepared.message); return; }
    for (Side side : {Side::Left, Side::Right}) {
        m_editing->modified[index(side)] = false; emit modifiedChanged(side, false);
    }
    const auto targets = m_editing->targets;
    const auto bom = m_editing->bom;
    const auto unsafe = m_editing->unsafe, encodingUnsafe = m_editing->encodingUnsafe, exists = m_editing->diskExists;
    const auto canonical = m_editing->canonicalTargets;
    const auto hashes = m_editing->diskHash;
    setComparison(prepared.comparison);
    setSaveTarget(Side::Left, targets[0]); setSaveTarget(Side::Right, targets[1]);
    m_editing->bom = bom;
    m_editing->unsafe = unsafe; m_editing->encodingUnsafe = encodingUnsafe;
    m_editing->canonicalTargets = canonical; m_editing->diskHash = hashes; m_editing->diskExists = exists;
    updateEditability();
}
void FileDiffWidget::setSaveTarget(Side side, const QString& path) {
    const int i = index(side);
    m_editing->targets[i] = path;
    const QFileInfo file(path);
    m_editing->diskExists[i] = file.exists();
    m_editing->canonicalTargets[i] = file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
    m_editing->diskHash[i] = file.exists() ? fingerprint(path) : QByteArray{};
    m_editing->unsafe[i] = m_binaryInput || (!path.isEmpty() && (!writable(file) || (file.exists() && m_editing->diskHash[i].isEmpty())));
    updateEditability();
    emit editableChanged(side, isEditable(side));
}
QString FileDiffWidget::saveTarget(Side side) const { return m_editing->targets[index(side)]; }
bool FileDiffWidget::copyChange(int blockIndex, Side source) {
    const Side target = source == Side::Left ? Side::Right : Side::Left;
    if (!isEditable(target) || m_editing->encodingUnsafe[index(source)] || isRecomputing() || m_viewMode != ViewMode::SideBySide || m_skipUnchanged ||
        blockIndex < 0 || blockIndex >= changes().size()) return false;
    const auto block = changes()[blockIndex];
    const auto from = source == Side::Left ? block.leftRange : block.rightRange;
    const auto to = target == Side::Left ? block.leftRange : block.rightRange;
    auto current = TextSnapshot::fromText(text(target));
    const auto input = TextSnapshot::fromText(text(source));
    for (int n = 0; n < to.count; ++n) current.lines.removeAt(to.start);
    for (int n = 0; n < from.count; ++n) current.lines.insert(to.start+n, input.lines[from.start+n]);
    if (to.end() == m_comparison->snapshot(target).lines.size() && from.end() == input.lines.size())
        current.finalNewline = input.finalNewline;
    if (current.lines.isEmpty()) current.finalNewline = false;
    auto* area = (target == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    area->undoStack()->push(new ReplaceText(area, text(target), normalized(current), m_syncingScroll));
    return true;
}

bool FileDiffWidget::save(Side side, QString* error, bool overwriteChanged) {
    const int i = index(side);
    const auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    if (!isEditable(side) || m_editing->targets[i].isEmpty()) return fail(QStringLiteral("Side is read-only or has no save target"));
    const QFileInfo file(m_editing->targets[i]);
    const auto canonical = file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
    if (canonical != m_editing->canonicalTargets[i]) return fail(QStringLiteral("Save target changed; select it again explicitly"));
    if (!writable(file)) return fail(QStringLiteral("Save target is not writable"));
    if (!overwriteChanged && (file.exists() != m_editing->diskExists[i] ||
        (file.exists() && fingerprint(file.filePath()) != m_editing->diskHash[i])))
        return fail(QStringLiteral("File changed on disk since loading or saving"));
    const auto& original = m_editing->cleanSnapshots[i];
    auto current = TextSnapshot::fromText(text(side), original.label, original.fileName);
    LineEnding preferred = LineEnding::LF;
    for (auto ending : original.lineEndings) if (ending != LineEnding::None && ending != LineEnding::Unknown) { preferred = ending; break; }
    QVector<LineEnding> endings(current.lines.size(), preferred);
    // Preserve endings of unchanged lines even in files with mixed delimiters.
    auto mapping = prepareComparison(original, current);
    if (mapping.status != PreparationStatus::Ready) return fail(mapping.message);
    for (const auto& hunk : mapping.comparison->diff().hunks) {
        if (hunk.type == diffcore::ChangeType::Equal) {
            for (int n = 0; n < hunk.leftRange.count; ++n) {
                const auto ending = original.lineEndings.value(hunk.leftRange.start+n, preferred);
                if (ending != LineEnding::None && ending != LineEnding::Unknown) endings[hunk.rightRange.start+n] = ending;
            }
        } else if (hunk.type == diffcore::ChangeType::Replace) {
            for (int n = 0; n < std::min(hunk.leftRange.count, hunk.rightRange.count); ++n) {
                const auto ending = original.lineEndings.value(hunk.leftRange.start+n, preferred);
                if (ending != LineEnding::None && ending != LineEnding::Unknown) endings[hunk.rightRange.start+n] = ending;
            }
        }
    }
    QString output;
    for (int n = 0; n < current.lines.size(); ++n) {
        output += current.lines[n];
        if (n+1 == current.lines.size() && !current.finalNewline.value_or(false)) { endings[n] = LineEnding::None; continue; }
        switch (endings[n]) {
            case LineEnding::CRLF: output += QStringLiteral("\r\n"); break;
            case LineEnding::CR: output += '\r'; break;
            default: output += '\n'; break;
        }
    }
    QByteArray bytes = output.toUtf8();
    if (m_editing->bom[i]) bytes.prepend("\xef\xbb\xbf");
    // Write the canonical target so a symlink itself is never replaced.
    if (!QDir().mkpath(QFileInfo(canonical).absolutePath())) return fail(QStringLiteral("Cannot create save target directory"));
    QSaveFile saved(canonical);
    if (!saved.open(QIODevice::WriteOnly)) return fail(saved.errorString());
    if (file.exists() && !saved.setPermissions(file.permissions())) return fail(saved.errorString());
    if (saved.write(bytes) != bytes.size() || !saved.commit()) return fail(saved.errorString());
    current.lineEndings = endings;
    m_editing->cleanSnapshots[i] = current; m_editing->cleanText[i] = text(side);
    m_editing->diskExists[i] = true; m_editing->diskHash[i] = fingerprint(canonical);
    if (m_editing->modified[i]) { m_editing->modified[i] = false; emit modifiedChanged(side, false); }
    auto* area = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    area->undoStack()->setClean();
    updateEditability();
    if (error) error->clear();
    return true;
}
}
