// ScrollSyncMapper computes synchronized scroll positions for side-by-side diff.
//
// Model:
//   A "sync line" is defined at  firstVisibleLine + threshold * viewportLines.
//   Given the scrolled side's firstVisibleLine, we:
//     1. Compute syncLine = firstVisibleLine + threshold * viewportLines
//     2. Map syncLine to the corresponding position on the other side
//     3. otherTop = correspondingLine - threshold * viewportLines
//
// Mapping rules per hunk type (from scrolled side's perspective):
//   Equal   — direct 1:1 mapping
//   Replace — proportional (handles unequal-height Replace blocks)
//   Delete/Insert (content only on one side) — maps to the boundary on the
//     other side, so that side stays still while this side scrolls past it

#ifndef DIFFMERGE_GUI_SCROLLSYNCMAPPER_H
#define DIFFMERGE_GUI_SCROLLSYNCMAPPER_H

#include <QVector>

#include <diffcore/DiffTypes.h>

#include "AlignedLineModel.h"  // for Side enum

namespace diffmerge::gui {

class ScrollSyncMapper {
public:
    ScrollSyncMapper() = default;

    void build(const diffcore::DiffResult& diff);

    // threshold ∈ (0, 1): fraction of viewport height where the sync line sits.
    // 0.5 = center; lower values = closer to top. Default 0.4.
    void setThreshold(double fraction);
    double threshold() const { return m_threshold; }

    // Given the scrolled side's firstVisibleLine and visible line count,
    // return firstVisibleLine for the other side.
    // otherDocLineCount is needed to clamp the result.
    int computeOtherTop(Side scrolledSide, int topLine, int viewportLines,
                        int otherDocLineCount) const;

private:
    // Returns the corresponding fractional line on the other side.
    double correspondingLine(Side from, double docLine) const;

    struct Entry {
        int lStart, lCount;
        int rStart, rCount;
        diffcore::ChangeType type;
    };

    QVector<Entry> m_entries;
    double m_threshold = 0.4;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_SCROLLSYNCMAPPER_H
