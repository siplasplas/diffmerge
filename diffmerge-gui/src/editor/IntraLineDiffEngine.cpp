#include <diffmerge/IntraLineDiffEngine.h>

#include <algorithm>

#include <diffcore/SequenceDiff.h>

namespace diffmerge::gui {

IntraLineDiffEngine::LinePairResult
IntraLineDiffEngine::diffLinePair(const QString& left, const QString& right) {
    LinePairResult out;
    if (left.isEmpty() && right.isEmpty()) return out;

    // QString elements are UTF-16 units, matching the editor's column offsets.
    const auto result = diffcore::SequenceDiff::compute(left, right);

    for (const auto& h : result.hunks) {
        switch (h.type) {
        case diffcore::ChangeType::Delete:
            if (h.leftRange.count > 0)
                out.leftRanges.append({h.leftRange.start, h.leftRange.count});
            break;
        case diffcore::ChangeType::Insert:
            if (h.rightRange.count > 0)
                out.rightRanges.append({h.rightRange.start, h.rightRange.count});
            break;
        case diffcore::ChangeType::Replace:
            if (h.leftRange.count > 0)
                out.leftRanges.append({h.leftRange.start, h.leftRange.count});
            if (h.rightRange.count > 0)
                out.rightRanges.append({h.rightRange.start, h.rightRange.count});
            break;
        default:
            break;
        }
    }
    return out;
}

IntraLineDiffEngine::Result
IntraLineDiffEngine::compute(const diffcore::DiffResult& diff,
                             const QStringList& leftLines,
                             const QStringList& rightLines) {
    Result out;
    out.leftRanges.resize(leftLines.size());
    out.rightRanges.resize(rightLines.size());

    for (const auto& hunk : diff.hunks) {
        if (hunk.type != diffcore::ChangeType::Replace) continue;

        const int lStart  = hunk.leftRange.start;
        const int lCount  = hunk.leftRange.count;
        const int rStart  = hunk.rightRange.start;
        const int rCount  = hunk.rightRange.count;
        const int paired  = std::min(lCount, rCount);

        // Paired lines: run char-level diff.
        for (int i = 0; i < paired; ++i) {
            auto pr = diffLinePair(leftLines[lStart + i], rightLines[rStart + i]);
            out.leftRanges[lStart + i]  = std::move(pr.leftRanges);
            out.rightRanges[rStart + i] = std::move(pr.rightRanges);
        }

        // Extra lines on the longer side: entire line is changed.
        for (int i = paired; i < lCount; ++i) {
            const int len = leftLines[lStart + i].length();
            if (len > 0) out.leftRanges[lStart + i].append({0, len});
        }
        for (int i = paired; i < rCount; ++i) {
            const int len = rightLines[rStart + i].length();
            if (len > 0) out.rightRanges[rStart + i].append({0, len});
        }
    }
    return out;
}

}  // namespace diffmerge::gui
