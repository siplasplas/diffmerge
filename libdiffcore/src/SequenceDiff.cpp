#include "diffcore/SequenceDiff.h"

#include <algorithm>

namespace diffcore::detail {

namespace {

using internal::Block;
using internal::EditType;

// Map internal EditType to public ChangeType (one-to-one).
ChangeType toChangeType(EditType t) {
    switch (t) {
        case EditType::Insert: return ChangeType::Insert;
        case EditType::Delete: return ChangeType::Delete;
        case EditType::Equal:  return ChangeType::Equal;
    }
    return ChangeType::Equal;  // Unreachable, silence compiler.
}

// Build a single Hunk from a Block, treating X as left and Y as right.
Hunk hunkFromBlock(const Block& b, ChangeType type) {
    Hunk h;
    h.type = type;
    h.leftRange.start = b.startX;
    h.leftRange.count = b.endX - b.startX;
    h.rightRange.start = b.startY;
    h.rightRange.count = b.endY - b.startY;
    return h;
}

// If the engine swapped A and B, blocks reference swapped axes.
// Undo that so X always means "left" and Y always means "right".
void unswapBlocks(std::vector<Block>& blocks, ComputationControl* control) {
    for (Block& b : blocks) {
        checkpoint(control);
        std::swap(b.startX, b.startY);
        std::swap(b.endX, b.endY);
        if (b.type == EditType::Insert) b.type = EditType::Delete;
        else if (b.type == EditType::Delete) b.type = EditType::Insert;
    }
}

// Build the raw hunk list directly from blocks (no merging).
std::vector<Hunk> rawHunksFromBlocks(const std::vector<Block>& blocks, ComputationControl* control) {
    std::vector<Hunk> hunks;
    hunks.reserve(blocks.size());
    for (const Block& b : blocks) {
        checkpoint(control);
        hunks.push_back(hunkFromBlock(b, toChangeType(b.type)));
    }
    return hunks;
}

// Adjacent Delete+Insert (or Insert+Delete) pairs represent replaced
// regions. Merge them into a single Replace hunk covering both ranges.
std::vector<Hunk> mergeReplaceHunks(const std::vector<Hunk>& input, ComputationControl* control) {
    std::vector<Hunk> out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        checkpoint(control);
        const Hunk& current = input[i];
        const bool isIns = current.type == ChangeType::Insert;
        const bool isDel = current.type == ChangeType::Delete;
        if ((isIns || isDel) && i + 1 < input.size()) {
            const Hunk& next = input[i + 1];
            const bool pair = (isIns && next.type == ChangeType::Delete) ||
                              (isDel && next.type == ChangeType::Insert);
            if (pair) {
                Hunk merged;
                merged.type = ChangeType::Replace;
                // Take left range from whichever hunk is a Delete
                // (covers lines in left file), right range from Insert.
                const Hunk& del = isDel ? current : next;
                const Hunk& ins = isIns ? current : next;
                merged.leftRange = del.leftRange;
                merged.rightRange = ins.rightRange;
                out.push_back(merged);
                ++i;  // Skip the consumed next hunk.
                continue;
            }
        }
        out.push_back(current);
    }
    return out;
}

// Merge consecutive hunks that share the same ChangeType into one hunk.
// This consolidates runs that the O(NP) backtracker emits as separate
// single-element blocks (e.g. two adjacent Insert{1} → one Insert{2}).
std::vector<Hunk> coalesceAdjacentHunks(const std::vector<Hunk>& input, ComputationControl* control) {
    if (input.empty()) return {};
    std::vector<Hunk> out;
    out.push_back(input[0]);
    for (size_t i = 1; i < input.size(); ++i) {
        checkpoint(control);
        Hunk& last = out.back();
        const Hunk& h = input[i];
        if (h.type == last.type) {
            last.leftRange.count  += h.leftRange.count;
            last.rightRange.count += h.rightRange.count;
        } else {
            out.push_back(h);
        }
    }
    return out;
}

}  // namespace

SequenceDiffResult sequenceResultFromBlocks(std::vector<internal::Block> blocks,
                                            int leftSize, int rightSize, int editDistance,
                                            bool swapped, const SequenceDiffOptions& options, ComputationControl* control) {
    if (swapped) unswapBlocks(blocks, control);
    auto hunks = rawHunksFromBlocks(blocks, control);
    if (options.mergeReplaceHunks) hunks = mergeReplaceHunks(hunks, control);
    if (options.coalesceAdjacentSameType) hunks = coalesceAdjacentHunks(hunks, control);
    return {std::move(hunks), leftSize, rightSize, editDistance};
}

}  // namespace diffcore::detail
