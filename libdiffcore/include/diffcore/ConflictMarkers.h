#pragma once
#include <QByteArray>
#include <QString>
#include <QSet>
#include <QVector>
#include <optional>
#include "ComputationControl.h"
#include "DiffTypes.h"

namespace diffcore {
struct ByteRange {
    qint64 start = 0, length = 0;
    qint64 end() const { return start + length; }
};
enum class ConflictStatus { Complete, InvalidInput, Unsupported, ResourceLimit, Cancelled, Error };
struct ConflictLimits {
    quint64 maxInputBytes = 32 * 1024 * 1024;
    quint64 maxOutputBytes = 64 * 1024 * 1024;
    quint64 maxLines = 100000, maxLineUnits = 200000, maxCodeUnits = 4000000;
    quint64 maxConflicts = 4096, maxMetadataBytes = 1024 * 1024;
    quint64 maxWork = 20000000, maxTraceEntries = 1000000;
    quint64 maxCandidateBytes = 8 * 1024 * 1024;
};
struct MarkerOptions {
    int markerSize = 7;
    QSet<int> literalMarkerLines; // Zero-based input lines.
};
struct ConflictBlock {
    QString id;
    ByteRange envelope, left, right;
    std::optional<ByteRange> base;
    LineRange lines;
    QString leftLabel, baseLabel, rightLabel;
};
struct ParsedConflictFile {
    QByteArray bytes, sha256;
    MarkerOptions options;
    QVector<ConflictBlock> conflicts;
};
struct ParseConflictResult {
    ConflictStatus status = ConflictStatus::Error;
    ParsedConflictFile file;
    QString message;
    qint64 errorByteOffset = -1;
    int errorLine = -1;
    quint64 workPerformed = 0;
};
// Owns original bytes; every range indexes those bytes, including an initial BOM.
ParseConflictResult parseConflictFile(const QByteArray& input, const MarkerOptions& options = {},
    const ConflictLimits& limits = {}, const CancellationToken& cancellation = {});
namespace detail {
ParseConflictResult parseConflictFileControlled(const QByteArray&, const MarkerOptions&,
    const ConflictLimits&, ComputationControl&);
}
} // namespace diffcore
