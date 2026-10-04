#include "diffcore/DiffEngine.h"

#include "diffcore/LineInterner.h"
#include "diffcore/SequenceDiff.h"
#include "diffcore/SliderHeuristics.h"

namespace diffcore {

namespace {

// Only ASCII spaces and tabs are formatting candidates. Other characters,
// including spaces inside literals, remain significant in the final result.
QString alignmentKey(const QString& line, int level) {
    if (level == 1) {
        int start = 0, end = line.size();
        const auto spacing = [](QChar c) { return c == ' ' || c == '\t'; };
        while (start < end && spacing(line[start])) ++start;
        while (end > start && spacing(line[end - 1])) --end;
        return line.mid(start, end - start);
    }
    QString key;
    key.reserve(line.size());
    for (QChar c : line)
        if (c != ' ' && c != '\t') key.append(c);
    return key;
}

// Refine only unmatched intervals: exact anchors outrank trimmed anchors,
// which outrank matches ignoring interior spacing. Never move an anchor.
class SpacingAlignment {
public:
    SpacingAlignment(const QStringList& left, const QStringList& right,
                     const DiffOptions& options) : m_options(options) {
        LineInterner interner;
        auto ids = interner.intern(left, right, options);
        m_leftIds = std::move(ids.leftIds);
        m_rightIds = std::move(ids.rightIds);
        m_left = left;
        m_right = right;
    }

    std::vector<Hunk> compute() {
        refine(0, m_left.size(), 0, m_right.size(), 0);
        return std::move(m_hunks);
    }

private:
    void append(Hunk h) {
        if (!h.leftRange.count && !h.rightRange.count) return;
        if (h.type == ChangeType::Replace && !m_options.mergeReplaceHunks) {
            append({ChangeType::Delete, h.leftRange, {h.rightRange.start, 0}});
            append({ChangeType::Insert, {h.leftRange.end(), 0}, h.rightRange});
            return;
        }
        // Keep replacement anchors separate from unequal-height gaps so that
        // renderers pair the intended lines rather than pairing by block offset.
        if (m_options.coalesceAdjacentSameType && !m_hunks.empty()) {
            auto& prev = m_hunks.back();
            if (prev.type == h.type && prev.leftRange.end() == h.leftRange.start &&
                prev.rightRange.end() == h.rightRange.start &&
                (h.type != ChangeType::Replace ||
                 (prev.leftRange.count == prev.rightRange.count &&
                  h.leftRange.count == h.rightRange.count))) {
                prev.leftRange.count += h.leftRange.count;
                prev.rightRange.count += h.rightRange.count;
                return;
            }
        }
        m_hunks.push_back(h);
    }

    void refine(int ls, int lc, int rs, int rc, int level) {
        if (!lc || !rc) {
            append({!lc ? ChangeType::Insert : !rc ? ChangeType::Delete : ChangeType::Replace,
                    {ls, lc}, {rs, rc}});
            return;
        }
        std::vector<int> leftIds, rightIds;
        if (level == 0 || level == 3) {
            leftIds.assign(m_leftIds.begin() + ls, m_leftIds.begin() + ls + lc);
            rightIds.assign(m_rightIds.begin() + rs, m_rightIds.begin() + rs + rc);
        } else {
            QStringList leftKeys, rightKeys;
            for (int i = ls; i < ls + lc; ++i) leftKeys.append(alignmentKey(m_left[i], level));
            for (int i = rs; i < rs + rc; ++i) rightKeys.append(alignmentKey(m_right[i], level));
            LineInterner interner;
            auto ids = interner.intern(leftKeys, rightKeys, m_options);
            leftIds = std::move(ids.leftIds);
            rightIds = std::move(ids.rightIds);
        }
        const auto diff = SequenceDiff::compute(leftIds, rightIds,
            {level == 3 ? m_options.mergeReplaceHunks : true,
             level == 3 ? m_options.coalesceAdjacentSameType : true});
        for (size_t index = 0; index < diff.hunks.size(); ++index) {
            const auto& h = diff.hunks[index];
            const int l = ls + h.leftRange.start, r = rs + h.rightRange.start;
            if (level == 3) {
                append({h.type, {l, h.leftRange.count}, {r, h.rightRange.count}});
            } else if (h.type != ChangeType::Equal) {
                // The adapter may split one unmatched interval into Replace
                // plus Insert/Delete. Refine the whole interval together.
                int leftCount = h.leftRange.count, rightCount = h.rightRange.count;
                while (index + 1 < diff.hunks.size() &&
                       diff.hunks[index + 1].type != ChangeType::Equal) {
                    ++index;
                    leftCount += diff.hunks[index].leftRange.count;
                    rightCount += diff.hunks[index].rightRange.count;
                }
                refine(l, leftCount, r, rightCount, level + 1);
            } else {
                for (int i = 0; i < h.leftRange.count; ++i)
                    append({m_leftIds[l + i] == m_rightIds[r + i] ? ChangeType::Equal : ChangeType::Replace,
                            {l + i, 1}, {r + i, 1}});
            }
        }
    }

    const DiffOptions& m_options;
    QStringList m_left, m_right;
    std::vector<int> m_leftIds, m_rightIds;
    std::vector<Hunk> m_hunks;
};

// Fill in aggregate stats based on final hunks and insert/delete cost.
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
    if (opts.alignWhitespaceChanges) {
        DiffResult result;
        result.hunks = SpacingAlignment(left, right, opts).compute();
        result.leftLineCount = left.size();
        result.rightLineCount = right.size();
        int distance = 0;
        for (const auto& h : result.hunks)
            if (h.type != ChangeType::Equal) distance += h.leftRange.count + h.rightRange.count;
        result.stats = computeStats(result.hunks, distance);
        return result;
    }
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
