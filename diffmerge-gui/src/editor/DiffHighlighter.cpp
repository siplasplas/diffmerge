#include "DiffHighlighter.h"

namespace diffmerge::gui {

void DiffHighlighter::setData(const QVector<QVector<CharRange>>& changedRanges,
                              const QColor& strongBg) {
    m_ranges = changedRanges;
    m_attrs.clear();
    qce::TextAttribute attr;
    attr.background = strongBg;
    m_attrs.append(attr);
}

qce::HighlightState DiffHighlighter::initialState() const {
    return {{0}};  // line counter starts at 0
}

void DiffHighlighter::highlightLine(const QString& /*line*/,
                                    const qce::HighlightState& stateIn,
                                    QVector<qce::StyleSpan>& spans,
                                    qce::HighlightState& stateOut) const {
    spans.clear();
    const int lineNum = stateIn.contextStack.isEmpty()
                        ? 0 : stateIn.contextStack.first();
    stateOut.contextStack = {lineNum + 1};

    if (lineNum < 0 || lineNum >= m_ranges.size()) return;
    for (const auto& cr : m_ranges[lineNum]) {
        if (cr.length > 0)
            spans.append({cr.start, cr.length, 0});
    }
}

const QVector<qce::TextAttribute>& DiffHighlighter::attributes() const {
    return m_attrs;
}

}  // namespace diffmerge::gui
