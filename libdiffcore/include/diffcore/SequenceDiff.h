#ifndef DIFFCORE_SEQUENCEDIFF_H
#define DIFFCORE_SEQUENCEDIFF_H

#include <limits>
#include <stdexcept>
#include <utility>

#include "DiffTypes.h"
#include "ComputationControl.h"
#include "detail/Diff.h"

namespace diffcore {

struct SequenceDiffOptions {
    bool mergeReplaceHunks = true;
    bool coalesceAdjacentSameType = true;
};

struct SequenceDiffResult {
    // Hunk ranges index input elements, not necessarily lines. Empty ranges
    // identify insertion boundaries; all ranges use original left/right order.
    std::vector<Hunk> hunks;
    int leftSize = 0;
    int rightSize = 0;
    int editDistance = 0;  // Insert/delete distance; a replacement costs both.

    bool isIdentical() const {
        return hunks.empty() || (hunks.size() == 1 && hunks[0].type == ChangeType::Equal);
    }
};

namespace detail {
SequenceDiffResult sequenceResultFromBlocks(std::vector<internal::Block> blocks,
                                            int leftSize, int rightSize, int editDistance,
                                            bool swapped, const SequenceDiffOptions& options, ComputationControl* control);
}  // namespace detail

// Exact element comparison through the shared O(NP) engine. No normalization,
// hashing or line-specific slider heuristics are applied. Container must be
// copyable and provide value_type, size() and indexed elements comparable by ==.
// Examples: std::vector<int>, std::string, QString, std::span<const Token>.
// Coordinates are int; oversized inputs throw std::length_error.
class SequenceDiff {
public:
    template <typename Container>
    static SequenceDiffResult compute(const Container& left, const Container& right,
                                      const SequenceDiffOptions& options = {},
                                      ComputationControl* control = nullptr) {
        checkpoint(control);
        constexpr int maxSize = std::numeric_limits<int>::max();
        if (std::cmp_greater(left.size(), maxSize) || std::cmp_greater(right.size(), maxSize))
            throw std::length_error("Sequence diff input is too large");
        const int leftSize = static_cast<int>(left.size());
        const int rightSize = static_cast<int>(right.size());
        SequenceDiffResult result{{}, leftSize, rightSize, 0};
        if (leftSize == 0 || rightSize == 0) {
            if (leftSize != 0 || rightSize != 0) {
                result.hunks.push_back({leftSize ? ChangeType::Delete : ChangeType::Insert,
                                       {0, leftSize}, {0, rightSize}});
                result.editDistance = leftSize + rightSize;
            }
            return result;
        }

        bool identical = leftSize == rightSize;
        for (int i = 0; identical && i < leftSize; ++i) {
            checkpoint(control);
            identical = left[i] == right[i];
        }
        if (identical) {
            result.hunks.push_back({ChangeType::Equal, {0, leftSize}, {0, rightSize}});
            return result;
        }
        // Leave room for diagonal sentinels in the engine's signed indices.
        if (leftSize > maxSize - rightSize - 3)
            throw std::length_error("Sequence diff input is too large");
        internal::Diff<Container> engine(left, right, control);
        auto blocks = engine.walk();
        return detail::sequenceResultFromBlocks(std::move(blocks), leftSize, rightSize,
                                                engine.editDistance(), engine.swapped(), options, control);
    }
};

}  // namespace diffcore

#endif
