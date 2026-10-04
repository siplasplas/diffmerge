// Compose syntax foreground/font styles with character-diff backgrounds.
// The final state stack entry holds the original document line number;
// preceding entries retain the complete syntax context and captures.

#ifndef DIFFMERGE_GUI_DIFFHIGHLIGHTER_H
#define DIFFMERGE_GUI_DIFFHIGHLIGHTER_H

#include <memory>
#include <QColor>
#include <QVector>

#include <qce/IHighlighter.h>
#include <qce/HighlightState.h>
#include <qce/StyleSpan.h>
#include <qce/TextAttribute.h>

#include <diffmerge/IntraLineDiffEngine.h>

namespace diffmerge::gui {

class DiffHighlighter : public qce::IHighlighter {
public:
    using CharRange = IntraLineDiffEngine::CharRange;

    DiffHighlighter() = default;

    // Set highlight data.  changedRanges[docLine] lists which character runs
    // to highlight.  strongBg is the background color for those runs.
    void setData(const QVector<QVector<CharRange>>& changedRanges,
                 const QColor& strongBg);

    void setSyntax(std::unique_ptr<qce::IHighlighter> syntax);
    bool hasSyntax() const { return bool(m_syntax); }

    // qce::IHighlighter
    qce::HighlightState initialState() const override;
    void highlightLine(const QString& line,
                       const qce::HighlightState& stateIn,
                       QVector<qce::StyleSpan>& spans,
                       qce::HighlightState& stateOut) const override;
    const QVector<qce::TextAttribute>& attributes() const override;

private:
    void rebuildAttributes();
    std::unique_ptr<qce::IHighlighter> m_syntax;
    QColor m_strongBg;
    int m_syntaxAttributeCount = 0;
    QVector<QVector<CharRange>>  m_ranges;
    QVector<qce::TextAttribute>  m_attrs;   // Syntax, plain diff, then syntax with diff backgrounds.
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFHIGHLIGHTER_H
