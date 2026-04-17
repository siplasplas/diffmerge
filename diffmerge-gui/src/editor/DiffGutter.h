// DiffGutter paints line numbers for a DiffEditor. Placed on the outer
// edge of the editor (left for Left side, right for Right side).
//
// Placeholder rows (where fileLineNumber == -1 in the aligned model) show
// no number - just the gutter background.
//
// The gutter is a separate QWidget placed next to the QPlainTextEdit's
// viewport, scrolling together with it via updateRequest wiring in the
// parent DiffEditor.

#ifndef DIFFMERGE_GUI_DIFFGUTTER_H
#define DIFFMERGE_GUI_DIFFGUTTER_H

#include <QWidget>

#include "../common/ColorScheme.h"
#include "../fileview/AlignedLineModel.h"

class QPlainTextEdit;

namespace diffmerge::gui {

class DiffEditor;

class DiffGutter : public QWidget {
    Q_OBJECT
public:
    DiffGutter(DiffEditor* editor,
               Side side,
               const AlignedLineModel* model,
               QWidget* parent = nullptr);

    void setColorScheme(const ColorScheme& scheme);

    // Recompute width based on number of digits needed for the file's
    // largest line number. Called when the model changes.
    int computeWidth() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Paint a single line number (or nothing if placeholder) inside the
    // given vertical rect.
    void paintLineNumber(QPainter& painter, int alignedRow,
                         int top, int height);

    // Find the largest file line number that will be shown; used for width.
    int maxFileLineNumber() const;

    DiffEditor* m_editor;
    Side m_side;
    const AlignedLineModel* m_model;
    ColorScheme m_scheme;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFGUTTER_H
