#include <diffmerge/ConflictMarkers.h>
#include <diffcore/ConflictMarkers.h>

namespace diffmerge::gui {
MarkerImportResult importConflictMarkers(const PreparedMergeSession& session,
    const MarkerImportOptions& options, const MergeSessionLimits& limits,
    const diffcore::CancellationToken& cancellation) {
    MarkerImportResult result;
    if (!session.resultText() || !session.inputs().resultSeed) {
        result.message = QStringLiteral("Marker import requires an available RESULT text seed"); return result;
    }
    diffcore::ConflictLimits coreLimits;
    coreLimits.maxInputBytes = limits.maxInputBytes; coreLimits.maxLines = limits.maxInputLines;
    coreLimits.maxCodeUnits = limits.maxInputCodeUnits; coreLimits.maxLineUnits = limits.maxLineCodeUnits;
    coreLimits.maxConflicts = limits.maxConflicts; coreLimits.maxMetadataBytes = limits.maxMetadataBytes;
    coreLimits.maxWork = limits.maxWork;
    const auto parsed = diffcore::parseConflictFile(session.inputs().resultSeed->file.bytes,
        {options.markerSize, options.literalMarkerLines}, coreLimits, cancellation);
    result.workPerformed = parsed.workPerformed;
    if (parsed.status != diffcore::ConflictStatus::Complete) {
        result.status = parsed.status == diffcore::ConflictStatus::Cancelled ? MergeSessionStatus::Cancelled
            : parsed.status == diffcore::ConflictStatus::ResourceLimit ? MergeSessionStatus::ResourceLimit
            : parsed.status == diffcore::ConflictStatus::Unsupported ? MergeSessionStatus::Unsupported : MergeSessionStatus::Error;
        result.message = parsed.message; return result;
    }
    if (!parsed.file.conflicts.isEmpty() && !options.allowUnconfirmedMarkers &&
        (!session.inputs().hostConflicts || session.inputs().hostConflicts->isEmpty())) {
        result.message = QStringLiteral("Marker recognition requires host conflict metadata or explicit marker-file consent"); return result;
    }
    const auto bytes = [&parsed](diffcore::ByteRange range) { return parsed.file.bytes.mid(range.start, range.length); };
    for (const auto& block : parsed.file.conflicts) {
        ImportedConflict item;
        item.id = QStringLiteral("marker:%1").arg(block.envelope.start);
        item.resultLines = block.lines; item.resultBytes = {block.envelope.start, block.envelope.length};
        item.ours = bytes(block.left); item.theirs = bytes(block.right);
        if (block.base) item.base = bytes(*block.base);
        item.oursLabel = block.leftLabel; item.baseLabel = block.baseLabel; item.theirsLabel = block.rightLabel;
        result.conflicts.append(item);
    }
    result.status = MergeSessionStatus::Ready; return result;
}
} // namespace diffmerge::gui
