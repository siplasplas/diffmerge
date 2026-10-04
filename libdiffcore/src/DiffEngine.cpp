#include "diffcore/DiffEngine.h"
#include <climits>

#include "diffcore/LineInterner.h"
#include "diffcore/SequenceDiff.h"
#include "diffcore/SliderHeuristics.h"

namespace diffcore {

namespace {

// Only ASCII spaces and tabs are formatting candidates. Other characters,
// including spaces inside literals, remain significant in the final result.
QString alignmentKey(const QString& line, int level, ComputationControl* control) {
    if (level == 1) {
        int start = 0, end = line.size();
        const auto spacing = [](QChar c) { return c == ' ' || c == '\t'; };
        while (start < end && spacing(line[start])) { checkpoint(control); ++start; }
        while (end > start && spacing(line[end - 1])) { checkpoint(control); --end; }
        return line.mid(start, end - start);
    }
    QString key;
    key.reserve(line.size());
    for (QChar c : line) {
        checkpoint(control);
        if (c != ' ' && c != '\t') key.append(c);
    }
    return key;
}

// Refine only unmatched intervals: exact anchors outrank trimmed anchors,
// which outrank matches ignoring interior spacing. Never move an anchor.
class SpacingAlignment {
public:
    SpacingAlignment(const QStringList& left, const QStringList& right,
                     const DiffOptions& options, ComputationControl* control)
        : m_options(options), m_control(control) {
        LineInterner interner;
        auto ids = interner.intern(left, right, options, m_control);
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
        checkpoint(m_control);
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
        checkpoint(m_control, std::uint64_t(lc) + rc);
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
            for (int i = ls; i < ls + lc; ++i) leftKeys.append(alignmentKey(m_left[i], level, m_control));
            for (int i = rs; i < rs + rc; ++i) rightKeys.append(alignmentKey(m_right[i], level, m_control));
            LineInterner interner;
            auto ids = interner.intern(leftKeys, rightKeys, m_options, m_control);
            leftIds = std::move(ids.leftIds);
            rightIds = std::move(ids.rightIds);
        }
        const auto diff = SequenceDiff::compute(leftIds, rightIds,
            {level == 3 ? m_options.mergeReplaceHunks : true,
             level == 3 ? m_options.coalesceAdjacentSameType : true}, m_control);
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
    ComputationControl* m_control;
    QStringList m_left, m_right;
    std::vector<int> m_leftIds, m_rightIds;
    std::vector<Hunk> m_hunks;
};

// Fill in aggregate stats based on final hunks and insert/delete cost.
DiffStats computeStats(const std::vector<Hunk>& hunks, int editDistance, ComputationControl* control) {
    DiffStats s;
    s.editDistance = editDistance;
    for (const Hunk& h : hunks) {
        checkpoint(control);
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
                               const DiffOptions& opts, ComputationControl* control) {
    checkpoint(control);
    if (left.size() > INT_MAX - right.size() - 3)
        throw std::length_error("Line comparison input is too large");
    if (opts.alignWhitespaceChanges) {
        DiffResult result;
        result.hunks = SpacingAlignment(left, right, opts, control).compute();
        result.leftLineCount = left.size();
        result.rightLineCount = right.size();
        int distance = 0;
        for (const auto& h : result.hunks) {
            checkpoint(control);
            if (h.type != ChangeType::Equal) distance += h.leftRange.count + h.rightRange.count;
        }
        result.stats = computeStats(result.hunks, distance, control);
        return result;
    }
    LineInterner interner;
    auto ids = interner.intern(left, right, opts, control);
    auto sequence = SequenceDiff::compute(ids.leftIds, ids.rightIds,
        {opts.mergeReplaceHunks, opts.coalesceAdjacentSameType}, control);
    if (opts.applySliderHeuristics) {
        applySliderHeuristics(sequence.hunks, left, right, control);
    }

    DiffResult result;
    result.hunks = std::move(sequence.hunks);
    result.leftLineCount = sequence.leftSize;
    result.rightLineCount = sequence.rightSize;
    result.stats = computeStats(result.hunks, sequence.editDistance, control);
    return result;
}


ChangeCounts DiffEngine::countChanges(const QStringList& left, const QStringList& right,
                                      const DiffOptions& opts, const CountLimits& limits,
                                      CancellationToken cancellation) {
    ChangeCounts result;
    ComputationControl control(std::move(cancellation));
    control.setExternalCancellation(limits.cancel);
    if (limits.timeLimitMs > 0) control.setDeadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.timeLimitMs));
    try {
        control.step();
        if (left.size() > INT_MAX-3-right.size()) throw std::length_error("Too many lines to count");
        LineInterner interner;
        auto ids = interner.intern(left, right, opts, &control);
        auto& a = ids.leftIds;
        auto& b = ids.rightIds;
        int prefix = 0;
        while (prefix < int(a.size()) && prefix < int(b.size()) && a[prefix] == b[prefix]) { control.step(); ++prefix; }
        int m = int(a.size())-prefix, n = int(b.size())-prefix;
        while (m > 0 && n > 0 && a[prefix+m-1] == b[prefix+n-1]) { control.step(); --m; --n; }
        const int originalM = m, originalN = n;
        bool swapped = m > n;
        if (swapped) { std::swap(a, b); std::swap(m, n); }
        const auto exceeds = [&](int distance) { return limits.maxEditDistance > 0 && distance > limits.maxEditDistance; };
        if (exceeds(n-m)) { result.status = ChangeCounts::Status::TooManyDifferences; return result; }
        if (limits.maxEditDistance > 0) {
            QHash<int, int> balances;
            for (int i = 0; i < m; ++i) { control.step(); ++balances[a[prefix+i]]; }
            for (int i = 0; i < n; ++i) { control.step(); --balances[b[prefix+i]]; }
            int bound = 0;
            for (int balance : balances) { control.step(); bound += std::abs(balance); }
            if (exceeds(bound)) { result.status = ChangeCounts::Status::TooManyDifferences; return result; }
        }
        int distance = n+m;
        if (m > 0) {
            const int offset = m+1, delta = n-m;
            std::vector<int> frontier(size_t(m+n+3), -1);
            const auto extend = [&](int k) {
                control.step();
                int y = std::max(frontier[offset+k-1]+1, frontier[offset+k+1]);
                int x = y-k;
                while (x < m && y < n && a[prefix+x] == b[prefix+y]) { control.step(); ++x; ++y; }
                frontier[offset+k] = y;
            };
            for (int p = 0;; ++p) {
                control.step();
                distance = delta+2*p;
                if (exceeds(distance)) { result.status = ChangeCounts::Status::TooManyDifferences; return result; }
                for (int k = -p; k < delta; ++k) extend(k);
                for (int k = delta+p; k > delta; --k) extend(k);
                extend(delta);
                if (frontier[offset+delta] == n) break;
            }
        }
        if (exceeds(distance)) { result.status = ChangeCounts::Status::TooManyDifferences; return result; }
        control.step();
        const int common = (originalM+originalN-distance)/2;
        result.added = originalN-common; result.removed = originalM-common;
    } catch (const ComputationStopped& stopped) {
        result.status = stopped.reason == StopReason::Cancelled ? ChangeCounts::Status::Cancelled : ChangeCounts::Status::TimedOut;
    }
    return result;
}

}  // namespace diffcore
