#pragma once

#include <diffmerge/Comparison.h>
#include <QSet>

namespace diffmerge::gui {
enum class ViewMode { SideBySide, Unified };

// Original coordinates survive every display projection. A fold represents
// equal lines on both sides; its text is presentation only.
struct ViewRow {
    int leftLine = -1;
    int rightLine = -1;
    int hiddenCount = 0;
    diffcore::ChangeType type = diffcore::ChangeType::Equal;
};
class ViewProjection {
public:
    void build(const PreparedComparison& comparison, bool skip = false,
               int context = 3, const QSet<int>& opened = {});
    const QVector<ViewRow>& rows() const { return m_rows; }
    int rowFor(Side side, int originalLine) const;
private:
    QVector<ViewRow> m_rows;
    QVector<int> m_left, m_right;
};
}
