#include "DiffConnectorSplitter.h"

#include <QPainter>
#include <QPainterPath>
#include <QSplitterHandle>
#include <QMouseEvent>
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
        setMouseTracking(true);
        for (auto* editor : {left, right}) {
            connect(editor->edit()->area(), &qce::CodeEditArea::viewportChanged,
                    this, [this] { update(); });
            connect(editor, &DiffEditor::colorSchemeChanged, this, [this] { update(); });
        }
    }

    void setModel(const AlignedLineModel* model) { m_model = model; update(); }
    void setCopyActions(bool left, bool right, std::function<void(int, Side)> copy) {
        m_leftEditable = left; m_rightEditable = right; m_copy = std::move(copy); update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        m_hits.clear();
        updateMouseCursor();
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
        int blockIndex = -1;
        for (const auto& block : m_model->changeBlocks()) {
            ++blockIndex;
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
            if (!m_copy || (!m_leftEditable && !m_rightEditable)) continue;
            qreal center = (lt + lb + rt + rb) / 4;
            if (center < clipTop || center >= clipBottom) continue;
            const bool stacked = width()<16 && m_leftEditable && m_rightEditable;
            if (stacked && clipBottom-clipTop>=44)
                center = std::clamp(center,qreal(clipTop+22),qreal(clipBottom-22));
            const int buttonWidth = std::min(22, std::max(8, width()/2));
            const auto arrow = [&](Side source, int x) {
                const int offset = stacked ? (source == Side::Left ? 11 : -11) : 0;
                QRect hit(x, qRound(center)+offset-10, buttonWidth, 20);
                m_hits.append({hit, blockIndex, source});
            };
            if (m_leftEditable) arrow(Side::Right, 0);
            if (m_rightEditable) arrow(Side::Left, width()-buttonWidth);
        }
        // Controls sit above every connector, including neighbouring curves.
        for (const auto& hit : m_hits) {
            painter.fillRect(hit.rect, palette().button()); painter.setPen(palette().buttonText().color());
            painter.drawText(hit.rect, Qt::AlignCenter, hit.source == Side::Left ? QStringLiteral("→") : QStringLiteral("←"));
        }
        updateMouseCursor();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        m_mousePosition = event->position().toPoint();
        QSplitterHandle::mouseMoveEvent(event);
        updateMouseCursor();
    }
    void leaveEvent(QEvent* event) override {
        m_mousePosition = QPoint(-1,-1); updateMouseCursor();
        QSplitterHandle::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_copy) {
            for (const auto& hit : m_hits) if (hit.rect.contains(event->position().toPoint())) {
                const auto callback = m_copy; callback(hit.index, hit.source); event->accept(); return;
            }
        }
        QSplitterHandle::mousePressEvent(event);
    }

private:
    void updateMouseCursor() {
        for (const auto& hit : m_hits) if (hit.rect.contains(m_mousePosition)) {
            setCursor(Qt::ArrowCursor); return;
        }
        setCursor(Qt::SplitHCursor);
    }
    DiffEditor* m_left;
    DiffEditor* m_right;
    const AlignedLineModel* m_model;
    struct Hit { QRect rect; int index; Side source; };
    QVector<Hit> m_hits;
    QPoint m_mousePosition{-1,-1};
    bool m_leftEditable = false, m_rightEditable = false;
    std::function<void(int, Side)> m_copy;
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
    auto* result = new DiffConnectorHandle(this, m_left, m_right, m_model);
    result->setCopyActions(m_leftEditable, m_rightEditable, m_copy);
    return result;
}

void DiffConnectorSplitter::setCopyActions(bool left, bool right, std::function<void(int, Side)> copy) {
    m_leftEditable = left; m_rightEditable = right; m_copy = std::move(copy);
    for (int i=1; i<count(); ++i) static_cast<DiffConnectorHandle*>(handle(i))->setCopyActions(left, right, m_copy);
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
