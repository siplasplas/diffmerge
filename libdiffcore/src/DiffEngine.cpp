#include "diffcore/DiffEngine.h"

#include "diffcore/LineInterner.h"
#include "diffcore/SequenceDiff.h"
#include "diffcore/SliderHeuristics.h"

namespace diffcore {

namespace {

// Fill in aggregate stats based on final hunks and raw edit distance.
DiffStats computeStats(const std::vector<Hunk>& hunks, int editDistance) {
    DiffStats s;
    s.editDistance = editDistance;
    for (const Hunk& h : hunks) {
        switch (h.type) {
            case ChangeType::Insert:
                s.additions += h.rightRange.count;
                break;
            case ChangeType::Delete:
                s.deletions += h.leftRange.count;
                break;
            case ChangeType::Replace:
                s.deletions += h.leftRange.count;
                s.additions += h.rightRange.count;
                s.modifications += 1;
                break;
            case ChangeType::Equal:
                break;
        }
    }
    return s;
}

}  // namespace

DiffResult DiffEngine::compute(const QStringList& left,
                               const QStringList& right,
                               const DiffOptions& opts) {
    LineInterner interner;
    auto ids = interner.intern(left, right, opts);
    auto sequence = SequenceDiff::compute(ids.leftIds, ids.rightIds,
        {opts.mergeReplaceHunks, opts.coalesceAdjacentSameType});
    if (opts.applySliderHeuristics) {
        applySliderHeuristics(sequence.hunks, left, right);
    }

    DiffResult result;
    result.hunks = std::move(sequence.hunks);
    result.leftLineCount = sequence.leftSize;
    result.rightLineCount = sequence.rightSize;
    result.stats = computeStats(result.hunks, sequence.editDistance);
    return result;
}

}  // namespace diffcore
