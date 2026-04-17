#include "DiffEditor.h"

#include <QFontDatabase>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>

#include "DiffGutter.h"
#include "DiffSideMargin.h"

namespace diffmerge::gui {

DiffEditor::DiffEditor(Side side, QWidget* parent)
    : QPlainTextEdit(parent), m_side(side) {
    m_scheme = ColorScheme::forSystem();

    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Margin widgets are children of the editor but positioned within
    // the editor's viewport frame.
    m_gutter = new DiffGutter(this, m_side, nullptr, this);
    m_sideMargin = new DiffSideMargin(this, m_side, nullptr, this);
    m_gutter->setColorScheme(m_scheme);
    m_sideMargin->setColorScheme(m_scheme);

    // QPlainTextEdit emits updateRequest when the viewport needs repaint,
    // including on scroll. We forward it to the margin widgets so they
    // scroll in sync with the text.
    connect(this, &QPlainTextEdit::updateRequest,
            this, &DiffEditor::onUpdateRequest);

    // When the vertical scrollbar appears or disappears (e.g. after loading
    // a larger document), its range changes; relayout margin widgets so
    // they shift to avoid being covered by the scrollbar.
    connect(verticalScrollBar(), &QAbstractSlider::rangeChanged,
            this, [this]() { layoutMargins(); });

    updateViewportMargins();
}

void DiffEditor::setAlignedModel(const AlignedLineModel* model) {
    m_model = model;

    // Re-create margin widgets with the new model (simpler than adding a
    // setter API; margins are cheap QWidgets).
    delete m_gutter;
    delete m_sideMargin;
    m_gutter = new DiffGutter(this, m_side, m_model, this);
    m_sideMargin = new DiffSideMargin(this, m_side, m_model, this);
    m_gutter->setColorScheme(m_scheme);
    m_sideMargin->setColorScheme(m_scheme);
    m_gutter->show();
    m_sideMargin->show();

    if (m_model) {
        setPlainText(m_model->buildDocumentText(m_side));
    } else {
        clear();
    }

    updateViewportMargins();
    layoutMargins();
    viewport()->update();
}

void DiffEditor::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    if (m_gutter) m_gutter->setColorScheme(scheme);
    if (m_sideMargin) m_sideMargin->setColorScheme(scheme);
    viewport()->update();
}

int DiffEditor::gutterWidth() const {
    return m_gutter ? m_gutter->computeWidth() : 0;
}

int DiffEditor::sideMarginWidth() const {
    return m_sideMargin ? m_sideMargin->preferredWidth() : 0;
}

void DiffEditor::updateViewportMargins() {
    // Tell QPlainTextEdit to reserve space for our margin widgets so
    // text does not draw underneath them.
    const int gw = gutterWidth();
    const int sw = sideMarginWidth();
    if (m_side == Side::Left) {
        setViewportMargins(gw, 0, sw, 0);
    } else {
        setViewportMargins(sw, 0, gw, 0);
    }
}

int DiffEditor::verticalScrollBarWidth() const {
    // Qt places the vertical scrollbar on the right edge of contentsRect().
    // We need its width so the margin widgets can be shifted away from it,
    // otherwise the scrollbar paints over the gutter/side-margin.
    auto* sb = verticalScrollBar();
    if (!sb || !sb->isVisible()) return 0;
    return sb->width();
}

void DiffEditor::layoutMargins() {
    if (!m_gutter || !m_sideMargin) return;
    const QRect cr = contentsRect();
    const int gw = gutterWidth();
    const int sw = sideMarginWidth();
    const int sbw = verticalScrollBarWidth();

    if (m_side == Side::Left) {
        // Gutter on outer (left), side margin on inner (right).
        // Scrollbar (if visible) sits at cr.right(); push the side margin
        // LEFT of the scrollbar so digits/stripes are not covered.
        m_gutter->setGeometry(cr.left(), cr.top(), gw, cr.height());
        m_sideMargin->setGeometry(cr.right() - sbw - sw + 1, cr.top(),
                                   sw, cr.height());
    } else {
        // Gutter on outer (right), side margin on inner (left).
        // Scrollbar (if visible) sits at cr.right(); push the gutter
        // LEFT of the scrollbar.
        m_sideMargin->setGeometry(cr.left(), cr.top(), sw, cr.height());
        m_gutter->setGeometry(cr.right() - sbw - gw + 1, cr.top(),
                              gw, cr.height());
    }
}

void DiffEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    layoutMargins();
}

void DiffEditor::onUpdateRequest(const QRect& rect, int dy) {
    // Scroll the margins together with the viewport.
    if (dy != 0) {
        if (m_gutter) m_gutter->scroll(0, dy);
        if (m_sideMargin) m_sideMargin->scroll(0, dy);
    } else {
        if (m_gutter) m_gutter->update(0, rect.y(),
                                        m_gutter->width(), rect.height());
        if (m_sideMargin) m_sideMargin->update(0, rect.y(),
                                                m_sideMargin->width(),
                                                rect.height());
    }
}

void DiffEditor::paintPlaceholderGlyph(QPainter& painter, const QRect& r) {
    painter.setPen(m_scheme.placeholderFg);
    // A short sequence of em-dashes centered vertically.
    painter.drawText(r, Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("  — — — — — — —"));
}

void DiffEditor::paintLineBackgrounds(QPainter& painter) {
    if (!m_model) return;

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    const int viewportOffsetY = static_cast<int>(contentOffset().y());
    const int viewportWidth = viewport()->width();

    while (block.isValid()) {
        const QRectF geom = blockBoundingGeometry(block);
        const int top = static_cast<int>(geom.top()) + viewportOffsetY;
        const int height = static_cast<int>(geom.height());
        if (top > viewport()->height()) break;

        if (top + height >= 0 && blockNumber < m_model->rowCount()) {
            const AlignedRow& row = m_model->row(m_side, blockNumber);
            const QRect lineRect(0, top, viewportWidth, height);

            // Placeholder rows get a muted background + glyph.
            if (row.isPlaceholder()) {
                painter.fillRect(lineRect, m_scheme.placeholderBg);
                paintPlaceholderGlyph(painter, lineRect);
            } else {
                const QColor bg = m_scheme.backgroundFor(row.changeType);
                if (bg.isValid()) {
                    painter.fillRect(lineRect, bg);
                }
            }
        }
        block = block.next();
        ++blockNumber;
    }
}

void DiffEditor::paintEvent(QPaintEvent* event) {
    // Paint our custom line backgrounds UNDER the text.
    {
        QPainter bgPainter(viewport());
        paintLineBackgrounds(bgPainter);
    }
    // Then let QPlainTextEdit paint the text on top.
    QPlainTextEdit::paintEvent(event);
}

}  // namespace diffmerge::gui
