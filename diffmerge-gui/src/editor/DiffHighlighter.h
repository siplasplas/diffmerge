// DiffHighlighter highlights character-level changes within Replace lines.
//
// Implements qce::IHighlighter.  Uses HighlightState.contextStack[0] as a
// line counter (incremented on every call to highlightLine) so we know which
// doc line we are rendering without needing a separate line-index parameter.
//
// For each doc line, StyleSpans are emitted for every changed CharRange with
// the configured "strong" background color (replaceCharBg from ColorScheme).

#ifndef DIFFMERGE_GUI_DIFFHIGHLIGHTER_H
#define DIFFMERGE_GUI_DIFFHIGHLIGHTER_H

#include <QColor>
#include <QVector>

#include <qce/IHighlighter.h>
#include <qce/HighlightState.h>
#include <qce/StyleSpan.h>
#include <qce/TextAttribute.h>

#include "IntraLineDiffEngine.h"

namespace diffmerge::gui {

class DiffHighlighter : public qce::IHighlighter {
public:
    using CharRange = IntraLineDiffEngine::CharRange;

    DiffHighlighter() = default;

    // Set highlight data.  changedRanges[docLine] lists which character runs
    // to highlight.  strongBg is the background color for those runs.
    void setData(const QVector<QVector<CharRange>>& changedRanges,
                 const QColor& strongBg);

    // qce::IHighlighter
    qce::HighlightState initialState() const override;
    void highlightLine(const QString& line,
                       const qce::HighlightState& stateIn,
                       QVector<qce::StyleSpan>& spans,
                       qce::HighlightState& stateOut) const override;
    const QVector<qce::TextAttribute>& attributes() const override;

private:
    QVector<QVector<CharRange>>  m_ranges;
    QVector<qce::TextAttribute>  m_attrs;   // index 0 = strong background
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFHIGHLIGHTER_H
