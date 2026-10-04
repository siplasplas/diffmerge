#include <diffmerge/ViewProjection.h>
#include <algorithm>

namespace diffmerge::gui {
void ViewProjection::build(const PreparedComparison& comparison, bool skip,
                           int context, const QSet<int>& opened) {
    m_rows.clear();
    m_left.fill(-1, comparison.snapshot(Side::Left).lines.size());
    m_right.fill(-1, comparison.snapshot(Side::Right).lines.size());
    QVector<ViewRow> all;
    QVector<ViewRow> removed, added;
    const auto flush = [&] { all += removed; all += added; removed.clear(); added.clear(); };
    for (const auto& h : comparison.diff().hunks) {
        if (h.type == diffcore::ChangeType::Equal) {
            flush();
            for (int i = 0; i < h.leftRange.count; ++i)
                all.append({h.leftRange.start+i, h.rightRange.start+i});
        } else {
            for (int i = 0; i < h.leftRange.count; ++i)
                removed.append({h.leftRange.start+i, -1, 0, h.type});
            for (int i = 0; i < h.rightRange.count; ++i)
                added.append({-1, h.rightRange.start+i, 0, h.type});
        }
    }
    flush();
    context = std::max(0, context);
    for (int i = 0; i < all.size();) {
        if (!skip || all[i].type != diffcore::ChangeType::Equal) {
            m_rows.append(all[i++]); continue;
        }
        int end = i;
        while (end < all.size() && all[end].type == diffcore::ChangeType::Equal) ++end;
        const int first = i == 0 ? i : i + std::min(context, end-i);
        const int last = end == all.size() ? end : std::max(first, end-context);
        for (int j = i; j < first; ++j) m_rows.append(all[j]);
        if (last > first && !opened.contains(all[first].leftLine)) {
            auto row = all[first]; row.hiddenCount = last-first; m_rows.append(row);
        } else for (int j = first; j < last; ++j) m_rows.append(all[j]);
        for (int j = last; j < end; ++j) m_rows.append(all[j]);
        i = end;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows[i];
        for (int n = 0; n < std::max(1, row.hiddenCount); ++n) {
            if (row.leftLine >= 0) m_left[row.leftLine+n] = i;
            if (row.rightLine >= 0) m_right[row.rightLine+n] = i;
        }
    }
}
int ViewProjection::rowFor(Side side, int line) const {
    const auto& map = side == Side::Left ? m_left : m_right;
    return line >= 0 && line < map.size() ? map[line] : int(m_rows.size());
}
}
