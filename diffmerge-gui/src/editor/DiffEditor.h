// DiffEditor is a read-only QPlainTextEdit wired up with:
//   - DiffGutter on the OUTER edge (line numbers, skipping placeholders)
//   - DiffSideMargin on the INNER edge (colored stripes for changes)
//   - Custom paintEvent that fills line backgrounds based on ChangeType
//   - Placeholder glyphs ("— — —") for virtual lines without file content
//
// Edit is disabled (stage 2 is read-only); the widget is designed so that
// flipping setReadOnly(false) in stage 4 is trivial once recompute-on-edit
// is wired up.

#ifndef DIFFMERGE_GUI_DIFFEDITOR_H
#define DIFFMERGE_GUI_DIFFEDITOR_H

#include <QPlainTextEdit>
#include <QTextBlock>

#include "../common/ColorScheme.h"
#include "../fileview/AlignedLineModel.h"

namespace diffmerge::gui {

class DiffGutter;
class DiffSideMargin;

class DiffEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    DiffEditor(Side side, QWidget* parent = nullptr);

    // Set the model that drives line metadata. The editor does NOT own it;
    // typically FileDiffWidget holds one model shared with both editors.
    void setAlignedModel(const AlignedLineModel* model);

    // Apply a new color scheme; repaints everything.
    void setColorScheme(const ColorScheme& scheme);

    Side side() const { return m_side; }

    int gutterWidth() const;
    int sideMarginWidth() const;

    // Public wrappers exposing protected QPlainTextEdit members to our
    // margin widgets (Qt does not make these accessible via friendship).
    QTextBlock publicFirstVisibleBlock() const { return firstVisibleBlock(); }
    QPointF publicContentOffset() const { return contentOffset(); }
    QRectF publicBlockBoundingGeometry(const QTextBlock& b) const {
        return blockBoundingGeometry(b);
    }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    // Forward editor scroll/update events to the margin widgets.
    void onUpdateRequest(const QRect& rect, int dy);

private:
    // Reposition the gutter and side margin within the editor's frame.
    void layoutMargins();

    // Update the editor's viewport margins so text does not overlap the
    // gutter/side-margin widgets.
    void updateViewportMargins();

    // Width of the vertical scrollbar when visible, 0 otherwise. Used to
    // push margin widgets away from the scrollbar so they stay visible.
    int verticalScrollBarWidth() const;

    // Paint the background of each visible line according to ChangeType,
    // including a placeholder glyph for rows without content.
    void paintLineBackgrounds(QPainter& painter);

    // Paint the "— — —" glyph centered in a placeholder row's rect.
    void paintPlaceholderGlyph(QPainter& painter, const QRect& rect);

    Side m_side;
    const AlignedLineModel* m_model = nullptr;
    ColorScheme m_scheme;
    DiffGutter* m_gutter = nullptr;
    DiffSideMargin* m_sideMargin = nullptr;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFEDITOR_H
