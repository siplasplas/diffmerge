#pragma once
#include <QSet>
#include <diffmerge/MergeSession.h>

namespace diffmerge::gui {
struct MergeByteRange { qint64 start = 0, length = 0; };
struct ImportedConflict {
    QString id;
    diffcore::LineRange resultLines; // Includes marker delimiter lines.
    MergeByteRange resultBytes; // Includes original line terminators, excludes file BOM.
    QByteArray ours, theirs;
    std::optional<QByteArray> base; // Fragment only, not a reconstructed BASE file.
    QString oursLabel, baseLabel, theirsLabel;
};
struct MarkerImportOptions {
    int markerSize = 7; // Positive, configured delimiter run length.
    // Explicitly opening a marker file can supply this consent without host stages.
    bool allowUnconfirmedMarkers = false;
    QSet<int> literalMarkerLines{}; // Original RESULT lines explicitly treated as source text.
};
struct MarkerImportResult {
    MergeSessionStatus status = MergeSessionStatus::Error;
    QVector<ImportedConflict> conflicts; // Empty on any failure, never partial.
    QString message;
};
// Read-only, worker-safe import. RESULT bytes and host resolution states are never
// changed. Common text outside zdiff3 regions remains in the original seed.
MarkerImportResult importConflictMarkers(const PreparedMergeSession& session,
    const MarkerImportOptions& options = {}, const MergeSessionLimits& limits = {},
    const diffcore::CancellationToken& cancellation = {});
} // namespace diffmerge::gui
