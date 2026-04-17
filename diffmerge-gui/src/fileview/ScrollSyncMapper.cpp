#include "ScrollSyncMapper.h"

#include <cmath>

#include <QtGlobal>

namespace diffmerge::gui {

void ScrollSyncMapper::build(const diffcore::DiffResult& diff) {
    m_entries.clear();
    m_entries.reserve(static_cast<int>(diff.hunks.size()));
    for (const auto& h : diff.hunks) {
        m_entries.append({h.leftRange.start, h.leftRange.count,
                          h.rightRange.start, h.rightRange.count,
                          h.type});
    }
}

void ScrollSyncMapper::setThreshold(double fraction) {
    m_threshold = fraction;
}

double ScrollSyncMapper::correspondingLine(Side from, double docLine) const {
    for (const auto& e : m_entries) {
        const int  start      = (from == Side::Left) ? e.lStart : e.rStart;
        const int  count      = (from == Side::Left) ? e.lCount : e.rCount;
        const int  otherStart = (from == Side::Left) ? e.rStart : e.lStart;
        const int  otherCount = (from == Side::Left) ? e.rCount : e.lCount;

        if (count == 0) continue;  // no lines on this side in this hunk

        if (docLine >= start && docLine < start + count) {
            if (e.type == diffcore::ChangeType::Equal) {
                return otherStart + (docLine - start);
            }
            if (e.type == diffcore::ChangeType::Replace) {
                const double fraction = (docLine - start) / count;
                return otherStart + fraction * otherCount;
            }
            // Delete (from=Left) or Insert (from=Right): other side has zero
            // lines here — map to the boundary so the other side stays still.
            return static_cast<double>(otherStart);
        }
    }

    // docLine is past all hunks (e.g. fractional overshoot): extrapolate linearly.
    if (m_entries.isEmpty()) return docLine;
    const auto& last = m_entries.last();
    const int lastEnd  = (from == Side::Left) ? last.lStart + last.lCount
                                               : last.rStart + last.rCount;
    const int otherEnd = (from == Side::Left) ? last.rStart + last.rCount
                                               : last.lStart + last.lCount;
    return otherEnd + (docLine - lastEnd);
}

int ScrollSyncMapper::computeOtherTop(Side scrolled, int topLine,
                                      int viewportLines,
                                      int otherDocLineCount) const {
    if (m_entries.isEmpty()) return topLine;

    const double syncLine   = topLine + m_threshold * viewportLines;
    const double otherSync  = correspondingLine(scrolled, syncLine);
    const double otherTopD  = otherSync - m_threshold * viewportLines;
    const int    otherTop   = static_cast<int>(std::round(otherTopD));
    return qBound(0, otherTop, qMax(0, otherDocLineCount - 1));
}

}  // namespace diffmerge::gui
