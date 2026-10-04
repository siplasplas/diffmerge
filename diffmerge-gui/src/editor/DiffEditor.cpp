#include <diffmerge/DiffEditor.h>

#include "DiffHighlighter.h"

#include <QFontDatabase>
#include <QEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <qce/CodeEditArea.h>

namespace diffmerge::gui {

// Extend block colors and empty-side boundaries through the inner number rail.
class DiffLineNumberGutter : public qce::LineNumberGutter {
public:
    DiffLineNumberGutter(const qce::ITextDocument* doc, qce::CodeEditArea* area, Side side)
        : qce::LineNumberGutter(doc), m_area(area), m_side(side) {}

    void setData(const AlignedLineModel* model, const ColorScheme& scheme) {
        m_model = model;
        m_scheme = scheme;
    }

    void paint(QPainter& painter, const qce::ViewportState& vp, const QRect& rect) override {
        painter.save();
        int origin = rect.top();
        if (auto* rail = dynamic_cast<QWidget*>(painter.device()))
            origin = rail->mapFromGlobal(m_area->viewport()->mapToGlobal(QPoint())).y();
        const QRect visibleRect = rect.intersected(QRect(rect.x(), origin, rect.width(), vp.viewportHeight));
        painter.setClipRect(visibleRect);
        painter.fillRect(rect, m_scheme.gutterBg);
        if (m_model && vp.isValid()) {
            for (const auto& block : m_model->changeBlocks()) {
                const auto& range = block.range(m_side);
                const int y = origin + vp.contentOffsetY
                            + (range.start - vp.firstVisibleLine) * vp.lineHeight;
                if (range.isEmpty()) {
                    painter.setPen(m_scheme.stripeFor(block.type));
                    painter.drawLine(rect.left(), y, rect.right(), y);
                } else {
                    painter.fillRect(QRect(rect.x(), y, rect.width(), range.count * vp.lineHeight),
                                     m_scheme.backgroundFor(block.type));
                }
            }
        }
        painter.setPen(m_scheme.gutterFg);
        qce::LineNumberGutter::paint(painter, vp, QRect(rect.x(), origin, rect.width(), vp.viewportHeight));
        painter.restore();
    }

private:
    qce::CodeEditArea* m_area;
    Side m_side;
    const AlignedLineModel* m_model = nullptr;
    ColorScheme m_scheme;
};

// Paint missing-side boundaries without inserting lines into the document.
class ChangeBoundaryOverlay : public QWidget {
public:
    explicit ChangeBoundaryOverlay(qce::CodeEditArea* area, Side side)
        : QWidget(area->viewport()), m_area(area), m_side(side) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parentWidget()->rect());
        parentWidget()->installEventFilter(this);
        connect(area, &qce::CodeEditArea::viewportChanged,
                this, [this] { update(); });
    }

    void setBoundaries(const QVector<ChangeBlock>& blocks,
                       const ColorScheme& scheme) {
        m_blocks = blocks;
        m_scheme = scheme;
        update();
    }

    void setRevealOverlay(std::optional<diffcore::LineRange> range, const QVector<int>& boundaries) {
        m_emphasis = range;
        m_searchBoundaries = boundaries;
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == parentWidget() && event->type() == QEvent::Resize)
            setGeometry(parentWidget()->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& vp = m_area->viewportState();
        if (!vp.isValid()) return;
        QPainter painter(this);
        const auto drawBoundary = [&](int line) {
            const int y = vp.contentOffsetY + (line - vp.firstVisibleLine) * vp.lineHeight;
            painter.setPen(QColor(220, 160, 20));
            painter.drawLine(0, y, width() - 1, y);
        };
        if (m_emphasis) {
            if (m_emphasis->count == 0) drawBoundary(m_emphasis->start);
            else {
                const int y = vp.contentOffsetY + (m_emphasis->start - vp.firstVisibleLine) * vp.lineHeight;
                painter.fillRect(QRect(0, y, width(), m_emphasis->count * vp.lineHeight), QColor(255, 210, 40, 65));
            }
        }
        for (int line : m_searchBoundaries) drawBoundary(line);
        for (const auto& block : m_blocks) {
            const auto& range = block.range(m_side);
            if (!range.isEmpty()) continue;
            const int y = vp.contentOffsetY
                        + (range.start - vp.firstVisibleLine) * vp.lineHeight;
            if (y < 0 || y >= height()) continue;
            painter.setPen(m_scheme.stripeFor(block.type));
            painter.drawLine(0, y, width() - 1, y);
        }
    }

private:
    qce::CodeEditArea* m_area;
    Side m_side;
    std::optional<diffcore::LineRange> m_emphasis;
    QVector<int> m_searchBoundaries;
    QVector<ChangeBlock> m_blocks;
    ColorScheme m_scheme;
};

DiffEditor::DiffEditor(Side side, QWidget* parent)
    : QWidget(parent), m_side(side), m_scheme(ColorScheme::forSystem()) {

    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);
    m_edit->area()->setReadOnly(true);
    m_edit->area()->setWordWrap(false);
    m_boundaries = new ChangeBoundaryOverlay(m_edit->area(), m_side);

    const QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_edit->setFont(f);

    // Scrollbar on outer edge
    using SBS = qce::CodeEdit::ScrollBarSide;
    m_edit->setScrollBarSide(m_side == Side::Left ? SBS::Left : SBS::Right);

    // Line numbers on inner edge
    m_lineNumbers = std::make_unique<DiffLineNumberGutter>(m_doc, m_edit->area(), m_side);
    m_lineNumbers->setData(nullptr, m_scheme);
    m_lineNumbers->setFont(f);
    if (m_side == Side::Left) {
        m_edit->addRightMargin(m_lineNumbers.get());
    } else {
        m_edit->addLeftMargin(m_lineNumbers.get());
    }

    m_edit->area()->setLineBackgroundProvider([this](int docLine) -> QColor {
        if (docLine < 0 || docLine >= static_cast<int>(m_docLineChanges.size()))
            return {};
        return m_scheme.backgroundFor(m_docLineChanges[docLine]);
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_edit);
}

DiffEditor::~DiffEditor() {
    // Margins are non-owning in qcodeedit; detach before destroying the drawer.
    if (m_side == Side::Left) m_edit->removeRightMargin(m_lineNumbers.get());
    else m_edit->removeLeftMargin(m_lineNumbers.get());
}

void DiffEditor::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (m_edit && m_lineNumbers && m_followSystemPalette &&
        (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange))
        applyColorScheme(ColorScheme::forSystem());
}

void DiffEditor::setAlignedModel(const AlignedLineModel* model) {
    m_model = model;
    applyModel();
}

void DiffEditor::setIntraLineDiffs(
    const QVector<QVector<IntraLineDiffEngine::CharRange>>& ranges)
{
    m_intraLineDiffs = ranges;
    applyHighlighter();
}

void DiffEditor::setRevealOverlay(std::optional<diffcore::LineRange> range, const QVector<int>& boundaries) {
    m_boundaries->setRevealOverlay(range, boundaries);
}

void DiffEditor::setColorScheme(const ColorScheme& scheme) {
    m_followSystemPalette = false;
    applyColorScheme(scheme);
}

void DiffEditor::applyColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    m_lineNumbers->setData(m_model, m_scheme);
    if (m_highlighter) {
        m_highlighter->setData(m_intraLineDiffs, scheme.replaceCharBg);
        m_edit->area()->setHighlighter(m_highlighter.get());
    }
    m_edit->area()->viewport()->update();
    m_boundaries->setBoundaries(m_model ? m_model->changeBlocks()
                                       : QVector<ChangeBlock>{}, m_scheme);
    emit colorSchemeChanged();
    // Rails are separate child widgets and must repaint for explicit overrides.
    for (auto* child : m_edit->findChildren<QWidget*>()) child->update();
}

void DiffEditor::applyModel() {
    m_lineNumbers->setData(m_model, m_scheme);
    if (!m_model) {
        m_docLineChanges.clear();
        m_boundaries->setBoundaries({}, m_scheme);
        m_doc->setLines({});
        return;
    }

    m_doc->setLines(m_model->documentLines(m_side));

    m_docLineChanges = m_model->docLineChanges(m_side);
    m_boundaries->setBoundaries(m_model->changeBlocks(), m_scheme);
    m_edit->area()->viewport()->update();
}

void DiffEditor::applyHighlighter() {
    // Check if any line actually has ranges to highlight.
    const bool hasAny = std::any_of(
        m_intraLineDiffs.begin(), m_intraLineDiffs.end(),
        [](const QVector<IntraLineDiffEngine::CharRange>& v) { return !v.isEmpty(); });

    if (!hasAny) {
        m_edit->area()->setHighlighter(nullptr);
        m_highlighter.reset();
        return;
    }

    if (!m_highlighter)
        m_highlighter = std::make_unique<DiffHighlighter>();

    m_highlighter->setData(m_intraLineDiffs, m_scheme.replaceCharBg);
    m_edit->area()->setHighlighter(m_highlighter.get());
}

}  // namespace diffmerge::gui
