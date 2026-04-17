// DiffSideMargin paints colored stripes for rows that differ. Placed on
// the INNER edge of each editor (right side of Left editor, left side of
// Right editor). Eventually will also host apply-change arrows - for now,
// just the stripe.

#ifndef DIFFMERGE_GUI_DIFFSIDEMARGIN_H
#define DIFFMERGE_GUI_DIFFSIDEMARGIN_H

#include <QWidget>

#include "../common/ColorScheme.h"
#include "../fileview/AlignedLineModel.h"

class QPlainTextEdit;

namespace diffmerge::gui {

class DiffEditor;

class DiffSideMargin : public QWidget {
    Q_OBJECT
public:
    DiffSideMargin(DiffEditor* editor,
                   Side side,
                   const AlignedLineModel* model,
                   QWidget* parent = nullptr);

    void setColorScheme(const ColorScheme& scheme);

    int preferredWidth() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Paint a stripe for a single aligned row.
    void paintStripe(QPainter& painter, int alignedRow, int top, int height);

    DiffEditor* m_editor;
    Side m_side;
    const AlignedLineModel* m_model;
    ColorScheme m_scheme;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFSIDEMARGIN_H
