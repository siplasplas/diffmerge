#include "DiffConnectorSplitter.h"

#include <QPainter>
#include <QPainterPath>
#include <QSplitterHandle>
#include <algorithm>

#include <qce/CodeEditArea.h>

#include <diffmerge/AlignedLineModel.h>
#include <diffmerge/DiffEditor.h>

namespace diffmerge::gui {

namespace {

class DiffConnectorHandle : public QSplitterHandle {
public:
    DiffConnectorHandle(QSplitter* splitter, DiffEditor* left, DiffEditor* right,
                        const AlignedLineModel* model)
        : QSplitterHandle(Qt::Horizontal, splitter),
          m_left(left), m_right(right), m_model(model) {
        for (auto* editor : {left, right}) {
            connect(editor->edit()->area(), &qce::CodeEditArea::viewportChanged,
                    this, [this] { update(); });
            connect(editor, &DiffEditor::colorSchemeChanged, this, [this] { update(); });
        }
    }

    void setModel(const AlignedLineModel* model) { m_model = model; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        if (!m_model) return;
        auto* leftViewport = m_left->edit()->area()->viewport();
        auto* rightViewport = m_right->edit()->area()->viewport();
        const auto& left = m_left->edit()->area()->viewportState();
        const auto& right = m_right->edit()->area()->viewportState();
        if (!left.isValid() || !right.isValid()) return;

        const int leftOrigin = mapFromGlobal(leftViewport->mapToGlobal(QPoint(0, 0))).y();
        const int rightOrigin = mapFromGlobal(rightViewport->mapToGlobal(QPoint(0, 0))).y();
        const int clipTop = std::max(leftOrigin, rightOrigin);
        const int clipBottom = std::min(leftOrigin + leftViewport->height(),
                                        rightOrigin + rightViewport->height());
        if (clipBottom <= clipTop) return;
        painter.setClipRect(QRect(0, clipTop, width(), clipBottom - clipTop));
        painter.setRenderHint(QPainter::Antialiasing);

        const auto yFor = [](int boundary, const qce::ViewportState& vp, int origin) {
            return origin + vp.contentOffsetY
                 + qreal(boundary - vp.firstVisibleLine) * vp.lineHeight;
        };
        const qreal w = width();
        const qreal middle = w / 2;
        const auto& scheme = m_left->colorScheme();
        for (const auto& block : m_model->changeBlocks()) {
            const qreal lt = yFor(m_left->displayLine(block.leftRange.start), left, leftOrigin);
            const qreal lb = yFor(m_left->displayLine(block.leftRange.end()), left, leftOrigin);
            const qreal rt = yFor(m_right->displayLine(block.rightRange.start), right, rightOrigin);
            const qreal rb = yFor(m_right->displayLine(block.rightRange.end()), right, rightOrigin);
            // Keep offscreen endpoints intact: clipping must not flatten curves.
            if (std::max(lb, rb) < clipTop || std::min(lt, rt) >= clipBottom)
                continue;

            QPainterPath top;
            top.moveTo(0, lt);
            top.cubicTo(middle, lt, middle, rt, w, rt);
            QPainterPath bottom;
            bottom.moveTo(w, rb);
            bottom.cubicTo(middle, rb, middle, lb, 0, lb);
            QPainterPath fill = top;
            fill.lineTo(w, rb);
            fill.connectPath(bottom);
            fill.closeSubpath();
            painter.fillPath(fill, scheme.backgroundFor(block.type));
            painter.setPen(scheme.stripeFor(block.type));
            painter.drawPath(top);
            painter.drawPath(bottom);
        }
    }

private:
    DiffEditor* m_left;
    DiffEditor* m_right;
    const AlignedLineModel* m_model;
};

}  // namespace

DiffConnectorSplitter::DiffConnectorSplitter(DiffEditor* left, DiffEditor* right,
                                             const AlignedLineModel* model, QWidget* parent)
    : QSplitter(Qt::Horizontal, parent), m_left(left), m_right(right), m_model(model) {
    setHandleWidth(48);
    addWidget(left);
    addWidget(right);
    setSizes({1, 1});
    setChildrenCollapsible(false);
}

QSplitterHandle* DiffConnectorSplitter::createHandle() {
    return new DiffConnectorHandle(this, m_left, m_right, m_model);
}

void DiffConnectorSplitter::setModel(const AlignedLineModel* model) {
    m_model = model;
    for (int i = 1; i < count(); ++i)
        static_cast<DiffConnectorHandle*>(handle(i))->setModel(model);
}

void DiffConnectorSplitter::updateConnections() {
    for (int i = 1; i < count(); ++i) handle(i)->update();
}

}  // namespace diffmerge::gui
