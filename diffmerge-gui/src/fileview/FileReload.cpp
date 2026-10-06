#include <diffmerge/FileDiffWidget.h>
#include <diffmerge/DiffEditor.h>
#include "FileEditingState.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStringConverter>
#include <QUndoStack>
#include <qce/CodeEditArea.h>

namespace diffmerge::gui {
bool FileDiffWidget::reloadSide(Side side, bool discardModified) {
    const int i = side == Side::Left ? 0 : 1;
    const auto path = saveTarget(side);
    const auto fail = [this](const QString& message) { emit loadFailed(message); return false; };
    if (!m_comparison || path.isEmpty()) return fail(QStringLiteral("No file target to reload"));
    if (isModified(side) && !discardModified)
        return fail(QStringLiteral("Save or explicitly discard edits before reloading this side"));
    if (m_binaryInput) {
        if (isModified(Side::Left) || isModified(Side::Right)) return false;
        return loadFromPaths(saveTarget(Side::Left), saveTarget(Side::Right));
    }
    const QFileInfo before(path);
    if (!before.isFile()) return fail(QStringLiteral("Reload target is missing or is not a regular file: %1").arg(path));
    const auto beforeCanonical = before.canonicalFilePath();
    const auto beforeSize = before.size();
    const auto beforeTime = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot reload %1: %2").arg(path, file.errorString()));
    const auto byteFallback = [&] {
        if(isModified(Side::Left) || isModified(Side::Right))
            return fail(QStringLiteral("Save or discard edits before switching to byte comparison"));
        return loadByteComparison(saveTarget(Side::Left),saveTarget(Side::Right));
    };
    constexpr qint64 limit = 8 * 1024 * 1024;
    if (file.size() > limit || file.peek(8000).contains('\0')) return byteFallback();
    auto bytes = file.read(limit + 1);
    if (bytes.size() > limit) return byteFallback();
    if (file.error() != QFileDevice::NoError) return fail(file.errorString());
    const QFileInfo after(path);
    if (beforeCanonical != after.canonicalFilePath() || beforeSize != after.size() ||
        beforeTime != after.lastModified() || bytes.size() != after.size())
        return fail(QStringLiteral("File changed during reload; try refreshing again"));
    if (bytes.contains('\0')) return byteFallback();
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    const bool bom = bytes.startsWith("\xef\xbb\xbf");
    if (bom) bytes.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    const bool encodingUnsafe = decoder.hasError();
    const auto& old = m_comparison->snapshot(side);
    auto replacement = TextSnapshot::fromText(decoded, old.label, old.fileName);
    const Side other = side == Side::Left ? Side::Right : Side::Left;
    const auto& otherOld = m_comparison->snapshot(other);
    const auto retained = m_editing->raw[1-i] ? TextSnapshot::fromText(text(other), otherOld.label, otherOld.fileName) : otherOld;
    const auto result = prepareComparison(i == 0 ? replacement : retained, i == 0 ? retained : replacement, m_options);
    if(result.status==PreparationStatus::ResourceLimit) return byteFallback();
    if (result.status != PreparationStatus::Ready) return fail(result.message);

    QScopedValueRollback<bool> installing(m_editing->installing, true);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    QScopedValueRollback<bool> navigating(m_navigating, true);
    auto* area = (i == 0 ? m_leftEditor : m_rightEditor)->edit()->area();
    auto* otherArea = (i == 0 ? m_rightEditor : m_leftEditor)->edit()->area();
    const int top = area->verticalScrollBar()->value(), otherTop = otherArea->verticalScrollBar()->value();
    const int horizontal = m_horizontalOffset;
    const auto cursor = area->cursorPosition();
    ++m_editing->generation; m_editing->cancellation.requestCancellation();
    m_editing->timer->stop(); m_editing->pending = false;
    clearSearchHighlights();
    m_editing->raw[i] = false;
    installEditedComparison(result.comparison);
    if (!m_skipUnchanged) (i == 0 ? m_leftEditor : m_rightEditor)->setAlignedModel(m_model);
    m_editing->cleanSnapshots[i] = replacement;
    m_editing->cleanText[i] = replacement.lines.join('\n') + (replacement.finalNewline.value_or(false) ? QStringLiteral("\n") : QString{});
    m_editing->bom[i] = bom; m_editing->encodingUnsafe[i] = encodingUnsafe;
    setSaveTarget(side, path);
    m_editing->canonicalTargets[i] = after.canonicalFilePath();
    m_editing->diskHash[i] = hash; m_editing->diskExists[i] = true;
    const bool wasModified = m_editing->modified[i];
    m_editing->modified[i] = false;
    updateEditability(); area->undoStack()->clear();
    const int line = std::min(cursor.line, std::max(0, area->document()->lineCount()-1));
    area->setCursorPosition({line, std::min(cursor.column, int(area->document()->lineAt(line).size()))});
    area->verticalScrollBar()->setValue(top); otherArea->verticalScrollBar()->setValue(otherTop);
    area->horizontalScrollBar()->setValue(horizontal); otherArea->horizontalScrollBar()->setValue(horizontal);
    if (wasModified) emit modifiedChanged(side, false);
    emit editableChanged(side, isEditable(side));
    return true;
}
}
