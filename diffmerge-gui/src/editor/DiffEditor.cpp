#include "DiffEditor.h"

#include <QFontDatabase>
#include <QEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <qce/CodeEditArea.h>

namespace diffmerge::gui {

// Paint missing-side boundaries without inserting lines into the document.
class ChangeBoundaryOverlay : public QWidget {
public:
    explicit ChangeBoundaryOverlay(qce::CodeEditArea* area)
        : QWidget(area->viewport()), m_area(area) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parentWidget()->rect());
        parentWidget()->installEventFilter(this);
        connect(area, &qce::CodeEditArea::viewportChanged,
                this, [this] { update(); });
    }

    void setBoundaries(const QVector<AlignedLineModel::FillerInfo>& fillers,
                       const ColorScheme& scheme) {
        m_fillers = fillers;
        m_scheme = scheme;
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
        for (const auto& filler : m_fillers) {
            // Padding inside a replacement is not a one-sided change.
            if (filler.changeType != diffcore::ChangeType::Insert &&
                filler.changeType != diffcore::ChangeType::Delete)
                continue;
            const int y = vp.contentOffsetY
                        + (filler.beforeDocLine - vp.firstVisibleLine) * vp.lineHeight;
            if (y < 0 || y >= height()) continue;
            painter.setPen(m_scheme.stripeFor(filler.changeType));
            painter.drawLine(0, y, width() - 1, y);
        }
    }

private:
    qce::CodeEditArea* m_area;
    QVector<AlignedLineModel::FillerInfo> m_fillers;
    ColorScheme m_scheme;
};

DiffEditor::DiffEditor(Side side, QWidget* parent)
    : QWidget(parent), m_side(side), m_scheme(ColorScheme::forSystem()) {

    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);
    m_edit->area()->setReadOnly(true);
    m_edit->area()->setWordWrap(false);
    m_boundaries = new ChangeBoundaryOverlay(m_edit->area());

    const QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_edit->setFont(f);

    // Scrollbar on outer edge
    using SBS = qce::CodeEdit::ScrollBarSide;
    m_edit->setScrollBarSide(m_side == Side::Left ? SBS::Left : SBS::Right);

    // Line numbers on inner edge
    m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
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

void DiffEditor::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    if (m_highlighter) {
        m_highlighter->setData(m_intraLineDiffs, scheme.replaceCharBg);
        m_edit->area()->setHighlighter(m_highlighter.get());
    }
    m_edit->area()->viewport()->update();
    m_boundaries->setBoundaries(m_model ? m_model->fillerRanges(m_side)
                                       : QVector<AlignedLineModel::FillerInfo>{}, m_scheme);
}

void DiffEditor::applyModel() {
    if (!m_model) {
        m_docLineChanges.clear();
        m_boundaries->setBoundaries({}, m_scheme);
        m_doc->setLines({});
        return;
    }

    m_doc->setLines(m_model->documentLines(m_side));

    const int docCount = m_doc->lineCount();
    m_docLineChanges.resize(docCount);
    for (int i = 0; i < docCount; ++i) {
        m_docLineChanges[i] = m_model->docLineChangeType(m_side, i);
    }
    m_boundaries->setBoundaries(m_model->fillerRanges(m_side), m_scheme);
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
