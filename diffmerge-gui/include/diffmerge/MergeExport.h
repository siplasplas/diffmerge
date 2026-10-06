#pragma once
#include <diffmerge/ConflictMarkers.h>
#include <QMetaType>

namespace diffmerge::gui {
enum class MergeChoice { Unresolved, Ours, Theirs, OursThenTheirs, TheirsThenOurs, Base, Delete, Manual };
// Half-open UTF-16 offsets in normalized RESULT text, not display rows or bytes.
struct MergeResultRange { int start = 0, length = 0; };
struct MergeEditableConflict {
    QString id;
    MergeResultRange range;
    bool mapped = false;
    MergeResolutionState state = MergeResolutionState::Unresolved;
    MergeChoice choice = MergeChoice::Unresolved;
};
enum class MergeExportDisposition { Cancelled, Draft, Resolved };
enum class MergeFileAction { Keep, Delete };
// Structured drafts are host-retained buffers, not files suitable for Git.
enum class MergeDraftFormat { Markers, HostBuffer };
enum class MergeMarkerStyle { Preserve, Merge, Diff3, ZDiff3 };
struct MergeExportOptions {
    MergeExportDisposition disposition = MergeExportDisposition::Draft;
    MergeFileAction action = MergeFileAction::Keep;
    MergeDraftFormat draftFormat = MergeDraftFormat::Markers;
    // Explicit whole-file replacement; never inferred from fragment choices.
    std::optional<MergeSource> wholeFileSource;
    bool confirmWholeFileReplacement = false;
    // nullopt retains the RESULT seed metadata; an empty path is valid for buffers.
    std::optional<QByteArray> rawPath;
    std::optional<std::uint32_t> mode;
    bool confirmUnknownConflictState = false;
    bool confirmFileDeletion = false; // Explicit whole-file decision, not fragment Delete.
    MergeMarkerStyle markerStyle = MergeMarkerStyle::Preserve;
    int markerSize = 7;
    LineEnding markerEnding = LineEnding::LF;
    std::optional<QString> oursLabel, baseLabel, theirsLabel;
};
// Capture on the GUI thread; owned values can then be exported on a worker.
struct MergeExportInput {
    std::shared_ptr<const PreparedMergeSession> session;
    QByteArray resultBytes;
    QVector<MergeEditableConflict> conflicts;
    MarkerImportOptions markerOptions;
    bool writable = false;
    bool literalMarkerLinesReviewed = false; // Explicit review in current output coordinates.
};
struct MergeSerialization {
    bool utf8Bom = false;
    bool finalNewline = false;
    QVector<LineEnding> lineEndings; // One per output line; None for an unterminated last line.
};
struct MergeExportOutcome {
    MergeExportDisposition disposition = MergeExportDisposition::Cancelled;
    MergeFileAction action = MergeFileAction::Keep;
    MergeDraftFormat draftFormat = MergeDraftFormat::Markers;
    std::optional<MergeSource> wholeFileSource;
    QByteArray bytes, rawPath;
    std::optional<std::uint32_t> mode;
    MergeFileKind kind = MergeFileKind::RegularFile;
    std::optional<MergeSerialization> serialization; // Delete has no text payload.
    QVector<MergeEditableConflict> conflicts;
    // Retains all captured source identities, paths, modes and original bytes.
    std::shared_ptr<const PreparedMergeSession> capturedSession;
    QByteArray fingerprint;
    bool explicitlyCompleted = false; // Never means saved, staged or accepted by the host.
};
struct PrepareMergeExportResult {
    MergeSessionStatus status = MergeSessionStatus::Error;
    std::optional<MergeExportOutcome> outcome; // Present only for Ready; never partial.
    QString message;
};
PrepareMergeExportResult prepareMergeExport(const MergeExportInput& input,
    const MergeExportOptions& options = {}, const MergeSessionLimits& limits = {},
    const diffcore::CancellationToken& cancellation = {});
} // namespace diffmerge::gui

Q_DECLARE_METATYPE(diffmerge::gui::MergeExportInput)
Q_DECLARE_METATYPE(diffmerge::gui::MergeExportOptions)
