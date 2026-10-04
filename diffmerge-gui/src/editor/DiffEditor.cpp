#include <diffmerge/DiffEditor.h>

#include "DiffHighlighter.h"
#include "SyntaxLoader.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <qce/CodeEditArea.h>

namespace diffmerge::gui {

// Cache syntax over each complete original document, before mixing rows.
class ProjectedHighlighter : public qce::IHighlighter {
public:
    QVector<QVector<qce::StyleSpan>> lines;
    QVector<qce::TextAttribute> palette;
    qce::HighlightState initialState() const override {
        qce::HighlightState state; state.contextStack.append(0); return state;
    }
    void highlightLine(const QString&, const qce::HighlightState& in,
                       QVector<qce::StyleSpan>& spans, qce::HighlightState& out) const override {
        const int n = in.contextStack.isEmpty() ? 0 : in.contextStack.last();
        spans = n >= 0 && n < lines.size() ? lines[n] : QVector<qce::StyleSpan>{};
        out = initialState(); out.contextStack[0] = n+1;
    }
    const QVector<qce::TextAttribute>& attributes() const override { return palette; }
};

// A row's background in a projected view: folded rows have the placeholder colour; in the unified view an old
// line is red and a new one green, since both share one pane; otherwise the change type decides. Invalid means
// no fill.
static QColor rowBackground(const ViewRow& row, bool unified, const ColorScheme& scheme) {
    if (row.hiddenCount) return scheme.placeholderBg;
    if (unified && row.type != diffcore::ChangeType::Equal) {
        if (row.rightLine < 0) return scheme.removedBg;
        if (row.leftLine < 0) return scheme.addedBg;
    }
    return scheme.backgroundFor(row.type);
}

// Extend block colors and empty-side boundaries through the inner number rail.
class DiffLineNumberGutter : public qce::LineNumberGutter {
public:
    DiffLineNumberGutter(const qce::ITextDocument* doc, qce::CodeEditArea* area, Side side)
        : qce::LineNumberGutter(doc), m_area(area), m_side(side) {}

    void setData(const AlignedLineModel* model, const ColorScheme& scheme) {
        m_model = model;
        m_scheme = scheme;
    }

    void setProjection(const QVector<ViewRow>* rows, bool unified) {
        m_rows = rows; m_unified = unified;
        int maximum = 1;
        if (rows) for (const auto& row : *rows)
            maximum = std::max(maximum, std::max(row.leftLine, row.rightLine) + std::max(1, row.hiddenCount));
        m_digits = QString::number(maximum).size();
    }
    int preferredWidth(const qce::ViewportState& vp) const override {
        if (!m_rows) return qce::LineNumberGutter::preferredWidth(vp);
        return QFontMetrics(font()).horizontalAdvance(QString(m_digits, QLatin1Char('9')))
             * (m_unified ? 2 : 1) + (m_unified ? 32 : 12);
    }
    void paint(QPainter& painter, const qce::ViewportState& vp, const QRect& rect) override {
        painter.save();
        int origin = rect.top();
        if (auto* rail = dynamic_cast<QWidget*>(painter.device()))
            origin = rail->mapFromGlobal(m_area->viewport()->mapToGlobal(QPoint())).y();
        const QRect visibleRect = rect.intersected(QRect(rect.x(), origin, rect.width(), vp.viewportHeight));
        painter.setClipRect(visibleRect);
        painter.fillRect(rect, m_scheme.gutterBg);
        if (m_rows && vp.isValid()) {
            painter.setFont(font());
            for (int i = vp.firstVisibleLine; i < m_rows->size() && i <= vp.firstVisibleLine + vp.visibleLineCount(); ++i) {
                if (i < 0) continue;
                const auto& row = (*m_rows)[i];
                const int y = origin + vp.contentOffsetY + (i-vp.firstVisibleLine)*vp.lineHeight;
                const QRect lineRect(rect.x(), y, rect.width()-4, vp.lineHeight);
                // An invalid colour would fill black; unchanged rows keep the gutter background.
                if (const QColor background = rowBackground(row, m_unified, m_scheme); background.isValid())
                    painter.fillRect(lineRect, background);
                painter.setPen(row.type == diffcore::ChangeType::Equal || row.hiddenCount ? m_scheme.gutterFg
                                                                                         : m_scheme.gutterChangedFg);
                QString label;
                if (row.hiddenCount) label = QStringLiteral("⋯");
                else if (m_unified) {
                    const auto number = [](int n) { return n < 0 ? QString{} : QString::number(n+1); };
                    const int digits = m_digits;
                    label = number(row.leftLine).rightJustified(digits) + QStringLiteral(" ")
                          + number(row.rightLine).rightJustified(digits) + QStringLiteral(" ")
                          + (row.leftLine < 0 ? QStringLiteral("+") : row.rightLine < 0 ? QStringLiteral("−") : QStringLiteral(" "));
                } else label = QString::number((m_side == Side::Left ? row.leftLine : row.rightLine)+1);
                painter.drawText(lineRect, Qt::AlignRight | Qt::AlignVCenter, label);
            }
            painter.restore(); return;
        }
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
    const QVector<ViewRow>* m_rows = nullptr;
    bool m_unified = false;
    int m_digits = 1;
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
    m_edit->area()->setFoldingProvider(nullptr);
    m_edit->area()->viewport()->installEventFilter(this);
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
        if (m_projectedComparison && docLine < m_rows.size())
            return rowBackground(m_rows[docLine], m_unified, m_scheme);
        return m_scheme.backgroundFor(m_docLineChanges[docLine]);
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_edit);
}

DiffEditor::~DiffEditor() {
    m_edit->area()->setHighlighter(nullptr);
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
    m_edit->area()->setHighlighter(nullptr);
    m_projectedComparison.reset();
    m_projectedHighlighter.reset();
    m_rows.clear();
    m_originalToDisplay.clear();
    m_lineNumbers->setProjection(nullptr, false);
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

void DiffEditor::setSyntaxFileName(const QString& fileName) {
    if (m_syntaxFileName == fileName) return;
    m_syntaxFileName = fileName;
    reloadSyntaxDefinitions();
}

void DiffEditor::reloadSyntaxDefinitions() {
    if (m_projectedComparison) { applyProjection(); return; }
    if (!m_highlighter) m_highlighter = std::make_unique<DiffHighlighter>();
    // Detach the old palette before replacing its owned syntax rules.
    m_edit->area()->setHighlighter(nullptr);
    m_highlighter->setSyntax(loadSyntax(m_syntaxFileName, m_scheme.darkTheme, m_syntaxLanguage));
    applyHighlighter();
}

void DiffEditor::setColorScheme(const ColorScheme& scheme) {
    m_followSystemPalette = false;
    applyColorScheme(scheme);
}

void DiffEditor::applyColorScheme(const ColorScheme& scheme) {
    const bool syntaxThemeChanged = m_scheme.darkTheme != scheme.darkTheme;
    m_scheme = scheme;
    if (m_projectedComparison) { applyProjection(); emit colorSchemeChanged(); return; }
    if (syntaxThemeChanged) reloadSyntaxDefinitions();
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
    if (m_projectedComparison) return;
    // Check if any line actually has ranges to highlight.
    const bool hasAny = std::any_of(
        m_intraLineDiffs.begin(), m_intraLineDiffs.end(),
        [](const QVector<IntraLineDiffEngine::CharRange>& v) { return !v.isEmpty(); });

    if (!hasAny && (!m_highlighter || !m_highlighter->hasSyntax())) {
        m_edit->area()->setHighlighter(nullptr);
        m_highlighter.reset();
        return;
    }

    if (!m_highlighter)
        m_highlighter = std::make_unique<DiffHighlighter>();

    m_highlighter->setData(m_intraLineDiffs, m_scheme.replaceCharBg);
    m_edit->area()->setHighlighter(m_highlighter.get());
}


void DiffEditor::setProjection(std::shared_ptr<const PreparedComparison> comparison,
                               const QVector<ViewRow>& rows, bool unified, const QString& leftSyntaxFileName) {
    m_projectedComparison = std::move(comparison);
    m_rows = rows; m_unified = unified; m_leftSyntaxFileName = leftSyntaxFileName;
    applyProjection();
}
int DiffEditor::displayLine(int line) const {
    if (!m_projectedComparison) return line;
    return line >= 0 && line < m_originalToDisplay.size() ? m_originalToDisplay[line] : int(m_rows.size());
}
int DiffEditor::originalLine(int row) const {
    if (!m_projectedComparison) return row;
    if (row < 0 || row >= m_rows.size()) return int(m_originalToDisplay.size());
    const auto& r = m_rows[row];
    int line = m_side == Side::Left ? r.leftLine : r.rightLine;
    if (line < 0) line = m_side == Side::Left ? r.rightLine : r.leftLine;
    return line;
}
bool DiffEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_edit->area()->viewport() && event->type() == QEvent::MouseButtonPress && m_projectedComparison) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const auto& vp = m_edit->area()->viewportState();
        if (mouse->button() == Qt::LeftButton && vp.lineHeight > 0) {
            const int row = vp.firstVisibleLine + int((mouse->position().y()-vp.contentOffsetY)/vp.lineHeight);
            if (row >= 0 && row < m_rows.size() && m_rows[row].hiddenCount) {
                emit foldClicked(m_rows[row].leftLine); return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void DiffEditor::applyProjection() {
    if (!m_projectedComparison) return;
    m_edit->area()->setHighlighter(nullptr);
    auto cache = std::make_unique<ProjectedHighlighter>();
    QVector<QVector<qce::StyleSpan>> original[2];
    for (int s = 0; s < 2; ++s) {
        const Side side = s == 0 ? Side::Left : Side::Right;
        if (!m_unified && side != m_side) continue;
        const auto& snapshot = m_projectedComparison->snapshot(side);
        DiffHighlighter highlighter;
        QString language;
        const QString fileName = side == m_side ? m_syntaxFileName : m_leftSyntaxFileName;
        highlighter.setSyntax(loadSyntax(fileName, m_scheme.darkTheme, language));
        if (side == m_side) m_syntaxLanguage = language;
        const QColor strong = !m_unified ? m_scheme.replaceCharBg
                            : side == Side::Left ? m_scheme.removedCharBg : m_scheme.addedCharBg;
        highlighter.setData(side == Side::Left ? m_projectedComparison->highlights().leftRanges
                                              : m_projectedComparison->highlights().rightRanges, strong);
        const int offset = cache->palette.size();
        cache->palette += highlighter.attributes();
        auto state = highlighter.initialState();
        for (const auto& line : snapshot.lines) {
            QVector<qce::StyleSpan> spans;
            qce::HighlightState next;
            highlighter.highlightLine(line, state, spans, next); state = next;
            for (auto& span : spans) span.attributeId += offset;
            original[s].append(spans);
        }
    }
    QStringList text;
    m_docLineChanges.clear();
    m_originalToDisplay.fill(-1, m_projectedComparison->snapshot(m_side).lines.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows[i];
        const Side side = m_unified ? (row.rightLine >= 0 ? Side::Right : Side::Left) : m_side;
        const int n = side == Side::Left ? row.leftLine : row.rightLine;
        const int own = m_side == Side::Left ? row.leftLine : row.rightLine;
        if (own >= 0) for (int j = 0; j < std::max(1, row.hiddenCount); ++j) m_originalToDisplay[own+j] = i;
        text.append(row.hiddenCount ? QStringLiteral("⋯ %1 unchanged lines — click to show").arg(row.hiddenCount)
                                   : m_projectedComparison->snapshot(side).lines[n]);
        cache->lines.append(row.hiddenCount ? QVector<qce::StyleSpan>{} : original[side == Side::Left ? 0 : 1][n]);
        m_docLineChanges.append(row.type);
    }
    m_lineNumbers->setData(nullptr, m_scheme);
    m_lineNumbers->setProjection(&m_rows, m_unified);
    QVector<ChangeBlock> boundaries;
    if (!m_unified) for (auto block : m_projectedComparison->changes()) {
        const auto range = block.range(m_side);
        auto projected = diffcore::LineRange{displayLine(range.start), displayLine(range.end())-displayLine(range.start)};
        if (m_side == Side::Left) block.leftRange = projected; else block.rightRange = projected;
        boundaries.append(block);
    }
    m_boundaries->setBoundaries(boundaries, m_scheme);
    m_doc->setLines(text);
    m_projectedHighlighter = std::move(cache);
    m_edit->area()->setHighlighter(m_projectedHighlighter.get());
    for (auto* child : m_edit->findChildren<QWidget*>()) child->update();
}

}  // namespace diffmerge::gui
