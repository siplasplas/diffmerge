#include "DiffSideMargin.h"

#include <QPainter>
#include <QTextBlock>

#include "DiffEditor.h"

namespace diffmerge::gui {

namespace {

// Total width of the margin. A stripe sits on the INNER edge (the one
// closest to the other editor); the rest is padding for future arrows.
constexpr int kMarginTotalWidth = 16;
constexpr int kStripeWidth = 4;

}  // namespace

DiffSideMargin::DiffSideMargin(DiffEditor* editor,
                               Side side,
                               const AlignedLineModel* model,
                               QWidget* parent)
    : QWidget(parent), m_editor(editor), m_side(side), m_model(model) {
    m_scheme = ColorScheme::forSystem();
    setAutoFillBackground(false);
}

void DiffSideMargin::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    update();
}

int DiffSideMargin::preferredWidth() const {
    return kMarginTotalWidth;
}

QSize DiffSideMargin::sizeHint() const {
    return QSize(preferredWidth(), 0);
}

void DiffSideMargin::paintStripe(QPainter& painter, int alignedRow,
                                 int top, int height) {
    const AlignedRow& r = m_model->row(m_side, alignedRow);
    const QColor stripe = m_scheme.stripeFor(r.changeType);
    if (!stripe.isValid()) return;  // No stripe for Equal rows.

    // Stripe sits on the INNER edge.
    // Left side: inner edge is on the right (near other editor).
    // Right side: inner edge is on the left.
    const int x = (m_side == Side::Left) ? width() - kStripeWidth : 0;
    painter.fillRect(QRect(x, top, kStripeWidth, height), stripe);
}

void DiffSideMargin::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);

    painter.fillRect(rect(), m_scheme.sideMarginBg);

    if (!m_editor || !m_model) return;

    // Same visibility loop as DiffGutter (see that class for the
    // block iteration explanation).
    QTextBlock block = m_editor->publicFirstVisibleBlock();
    int blockNumber = block.blockNumber();
    const int viewportOffsetY =
        static_cast<int>(m_editor->publicContentOffset().y());

    while (block.isValid()) {
        const QRectF geom = m_editor->publicBlockBoundingGeometry(block);
        const int top = static_cast<int>(geom.top()) + viewportOffsetY;
        const int height = static_cast<int>(geom.height());
        if (top > this->height()) break;
        if (top + height >= 0 && blockNumber < m_model->rowCount()) {
            paintStripe(painter, blockNumber, top, height);
        }
        block = block.next();
        ++blockNumber;
    }
}

}  // namespace diffmerge::gui
