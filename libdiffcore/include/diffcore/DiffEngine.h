// High-level diff API. Takes two sequences of lines (QStringList) and
// produces a DiffResult that the GUI/CLI can render directly.
//
// With alignWhitespaceChanges, unmatched intervals are refined using trimmed
// and then space/tab-free keys. Exact anchors are retained and originals are
// compared using the ignore options, so alignment never hides spacing changes.
//
// Internally (strict mode):
//  1. LineInterner assigns integer IDs to unique (normalized) lines.
//  2. The templated O(NP) engine computes an edit script over the IDs.
//  3. Block results are translated into public Hunk objects, with an
//     optional merge step turning adjacent Delete+Insert into Replace.
//  4. If the engine swapped its inputs internally, the adapter swaps
//     coordinates back so the user always sees (left, right) as given.

#ifndef DIFFCORE_DIFFENGINE_H
#define DIFFCORE_DIFFENGINE_H

#include <QStringList>

#include "DiffTypes.h"
#include "ComputationControl.h"

namespace diffcore {

// Nonpositive limits are unlimited. The atomic flag is borrowed for the call.
struct CountLimits {
    int maxEditDistance = 0;
    int timeLimitMs = 0;
    const std::atomic_bool* cancel = nullptr;
};
struct ChangeCounts {
    enum class Status { Complete, TooManyDifferences, TimedOut, Cancelled };
    Status status = Status::Complete;
    int added = 0;
    int removed = 0; // Counts are valid only for Complete.
};
class DiffEngine {
public:
    // Exact normalized-line O(NP) counting, without trace, hunks or sliders.
    // Formatting alignment affects presentation only and is not used here.
    ChangeCounts countChanges(const QStringList& left, const QStringList& right,
                             const DiffOptions& opts = {}, const CountLimits& limits = {},
                             CancellationToken cancellation = {});
    DiffResult compute(const QStringList& left,
                       const QStringList& right,
                       const DiffOptions& opts = {},
                       ComputationControl* control = nullptr);
};

}  // namespace diffcore

#endif  // DIFFCORE_DIFFENGINE_H
