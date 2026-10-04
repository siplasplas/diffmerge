#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <diffcore/ComputationControl.h>
#include <diffmerge/AlignedLineModel.h>
#include <diffmerge/IntraLineDiffEngine.h>
#include <diffmerge/ScrollSyncMapper.h>

namespace diffmerge::gui {

enum class LineEnding { None, LF, CRLF, CR, Unknown };

// Lines omit their terminators. {} is an empty file; {""} with a final LF is
// one empty terminated line. Unknown metadata supports legacy QStringList input.
struct TextSnapshot {
    QStringList lines;
    std::optional<bool> finalNewline;
    QVector<LineEnding> lineEndings; // Empty = unknown, otherwise one per line.
    QString label; // Arbitrary host text; no path or revision interpretation.
    static TextSnapshot fromText(const QString& text, const QString& label = {});
};

struct PreparationLimits {
    std::uint64_t maxInputLines = 100000; // Combined left + right.
    std::uint64_t maxInputCodeUnits = 4000000; // UTF-16, combined including joining newlines.
    std::uint64_t maxLineCodeUnits = 200000;
    std::uint64_t maxWork = 20000000; // Aggregate algorithm work units.
    std::uint64_t maxTraceEntries = 1000000; // Aggregate O(NP) recorded steps.
};
struct ComparisonOptions {
    diffcore::DiffOptions diff = {.alignWhitespaceChanges = true};
    PreparationLimits limits;
};

enum class PreparationStatus { Ready, Cancelled, ResourceLimit, Error };
class PreparedComparison;
struct PrepareResult {
    PreparationStatus status = PreparationStatus::Error;
    std::shared_ptr<const PreparedComparison> comparison; // Non-null only for Ready.
    QString message;
    std::chrono::nanoseconds preparationTime{};
    std::uint64_t workPerformed = 0;
};
PrepareResult prepareComparison(const TextSnapshot& left, const TextSnapshot& right,
    const ComparisonOptions& options = {}, const diffcore::CancellationToken& cancellation = {});

// All buffers are owned, immutable and independent of worker-local variables.
// Preparation uses QtCore values only; no QObject, QWidget or GUI resources.
class PreparedComparison {
public:
    const TextSnapshot& snapshot(Side side) const { return side == Side::Left ? m_left : m_right; }
    const diffcore::DiffResult& diff() const { return m_diff; }
    const AlignedLineModel& model() const { return m_model; }
    const IntraLineDiffEngine::Result& highlights() const { return m_highlights; }
    const ScrollSyncMapper& scrollMapping() const { return m_mapping; }
    const QVector<ChangeBlock>& changes() const { return m_model.changeBlocks(); }
private:
    PreparedComparison() = default;
    TextSnapshot m_left, m_right;
    diffcore::DiffResult m_diff;
    AlignedLineModel m_model;
    IntraLineDiffEngine::Result m_highlights;
    ScrollSyncMapper m_mapping;
    friend PrepareResult prepareComparison(const TextSnapshot&, const TextSnapshot&,
        const ComparisonOptions&, const diffcore::CancellationToken&);
};

// One original line, UTF-16 columns, half-open [column, column + length).
// Zero length is a boundary. At lineCount only {lineCount, 0, 0} is valid.
struct TextRange {
    int line = 0;
    int column = 0;
    int length = 0;
};
} // namespace diffmerge::gui
