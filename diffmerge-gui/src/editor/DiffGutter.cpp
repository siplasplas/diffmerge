#include "DiffGutter.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTextBlock>

#include "DiffEditor.h"

namespace diffmerge::gui {

namespace {

// Horizontal padding around the number text.
constexpr int kGutterPaddingX = 6;

}  // namespace

DiffGutter::DiffGutter(DiffEditor* editor,
                       Side side,
                       const AlignedLineModel* model,
                       QWidget* parent)
    : QWidget(parent), m_editor(editor), m_side(side), m_model(model) {
    m_scheme = ColorScheme::forSystem();
    setAutoFillBackground(false);
}

void DiffGutter::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    update();
}

int DiffGutter::maxFileLineNumber() const {
    if (!m_model) return 0;
    int maxNum = 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const int n = m_model->row(m_side, r).fileLineNumber;
        if (n > maxNum) maxNum = n;
    }
    // 1-based display, so largest number shown is maxNum + 1.
    return maxNum + 1;
}

int DiffGutter::computeWidth() const {
    // Width for largest line number + padding on both sides.
    const int maxNum = maxFileLineNumber();
    int digits = 1;
    for (int n = maxNum; n >= 10; n /= 10) ++digits;
    const QFontMetrics fm(font());
    const int digitWidth = fm.horizontalAdvance(QLatin1Char('9'));
    return 2 * kGutterPaddingX + digits * digitWidth;
}

QSize DiffGutter::sizeHint() const {
    return QSize(computeWidth(), 0);
}

void DiffGutter::paintLineNumber(QPainter& painter, int alignedRow,
                                 int top, int height) {
    const AlignedRow& r = m_model->row(m_side, alignedRow);
    if (r.isPlaceholder()) {
        // No number for placeholder rows.
        return;
    }
    const QString numberText = QString::number(r.fileLineNumber + 1);
    painter.setPen(m_scheme.gutterFg);

    // Line numbers are right-aligned on both sides (JetBrains style),
    // with equal padding on both left and right edges of the gutter so
    // digits do not touch the window border or separator.
    const QRect rect(kGutterPaddingX, top,
                     width() - 2 * kGutterPaddingX, height);
    painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, numberText);
}

void DiffGutter::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);

    // Background.
    painter.fillRect(rect(), m_scheme.gutterBg);

    // Edge separator (thin line on the inner side).
    const int sepX = (m_side == Side::Left) ? width() - 1 : 0;
    painter.setPen(m_scheme.gutterSeparator);
    painter.drawLine(sepX, 0, sepX, height());

    if (!m_editor || !m_model) return;

    // Iterate visible blocks of the editor and paint one number per block.
    // QPlainTextEdit::firstVisibleBlock() + blockBoundingGeometry gives us
    // the vertical position of each visible line.
    QTextBlock block = m_editor->publicFirstVisibleBlock();
    int blockNumber = block.blockNumber();
    const int viewportOffsetY =
        static_cast<int>(m_editor->publicContentOffset().y());

    while (block.isValid()) {
        const QRectF geom = m_editor->publicBlockBoundingGeometry(block);
        const int top = static_cast<int>(geom.top()) + viewportOffsetY;
        const int height = static_cast<int>(geom.height());
        if (top > this->height()) break;  // Past the visible area
        if (top + height >= 0 && blockNumber < m_model->rowCount()) {
            paintLineNumber(painter, blockNumber, top, height);
        }
        block = block.next();
        ++blockNumber;
    }
}

}  // namespace diffmerge::gui
