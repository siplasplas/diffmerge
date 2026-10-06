#include <diffmerge/FileDiffWidget.h>

#include <QFile>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QStyle>
#include <QTextStream>
#include <QThread>
#include <qce/ExtraSelection.h>
#include <QVBoxLayout>

#include <algorithm>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>
#include <qce/ViewportState.h>

#include <diffmerge/DiffEditor.h>
#include <diffmerge/IntraLineDiffEngine.h>
#include "DiffConnectorSplitter.h"

namespace diffmerge::gui {

FileDiffWidget::FileDiffWidget(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

FileDiffWidget::~FileDiffWidget() {
    // The divider reads the model while painting; destroy it before the model.
    delete m_splitter;
}

void FileDiffWidget::setPathBarVisible(bool visible) {
    m_pathBar->setVisible(visible);
}

void FileDiffWidget::setNavigationBarVisible(bool visible) {
    m_navigationBar->setVisible(visible);
}

void FileDiffWidget::setupUi() {
    auto* vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    // Navigation bar
    auto* navBar    = new QWidget(this);
    m_navigationBar = navBar;
    navBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(2, 2, 2, 2);
    navLayout->setSpacing(2);

    m_backButton = new QToolButton(navBar);
    m_backButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogBack));
    m_backButton->setText(QStringLiteral("← Directories"));
    m_backButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_backButton->setToolTip(QStringLiteral("Back to directory view"));
    m_backButton->setAutoRaise(true);
    m_backButton->setVisible(false);
    navLayout->addWidget(m_backButton);

    m_backSep = new QFrame(navBar);
    m_backSep->setFrameShape(QFrame::VLine);
    m_backSep->setFrameShadow(QFrame::Sunken);
    m_backSep->setVisible(false);
    navLayout->addWidget(m_backSep);

    connect(m_backButton, &QToolButton::clicked, this, &FileDiffWidget::backRequested);

    m_prevButton = new QToolButton(navBar);
    m_prevButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_prevButton->setToolTip(QStringLiteral("Previous change"));
    m_prevButton->setAutoRaise(true);
    navLayout->addWidget(m_prevButton);

    m_nextButton = new QToolButton(navBar);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_nextButton->setToolTip(QStringLiteral("Next change"));
    m_nextButton->setAutoRaise(true);
    navLayout->addWidget(m_nextButton);

    m_navLabel = new QLabel(QStringLiteral("No changes"), navBar);
    m_navLabel->setObjectName(QStringLiteral("diffChangePosition"));
    m_navLabel->setMargin(4);
    navLayout->addWidget(m_navLabel);
    navLayout->addStretch();

    vLayout->addWidget(navBar);

    connect(m_prevButton, &QToolButton::clicked, this, &FileDiffWidget::navigateToPrev);
    connect(m_nextButton, &QToolButton::clicked, this, &FileDiffWidget::navigateToNext);

    // Path bar
    auto* pathBar    = new QWidget(this);
    m_pathBar = pathBar;
    pathBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* pathLayout = new QHBoxLayout(pathBar);
    pathLayout->setContentsMargins(4, 3, 4, 3);

    m_leftPathEdit = new QLineEdit(pathBar);
    m_leftPathEdit->setPlaceholderText(QStringLiteral("Left file..."));
    m_leftBrowse = new QToolButton(pathBar);
    m_leftBrowse->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    m_leftBrowse->setToolTip(QStringLiteral("Browse left file"));
    m_leftBrowse->setAutoRaise(true);

    m_rightPathEdit = new QLineEdit(pathBar);
    m_rightPathEdit->setPlaceholderText(QStringLiteral("Right file..."));
    m_rightBrowse = new QToolButton(pathBar);
    m_rightBrowse->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    m_rightBrowse->setToolTip(QStringLiteral("Browse right file"));
    m_rightBrowse->setAutoRaise(true);

    pathLayout->addWidget(m_leftPathEdit);
    pathLayout->addWidget(m_leftBrowse);
    pathLayout->addSpacing(8);
    pathLayout->addWidget(m_rightPathEdit);
    pathLayout->addWidget(m_rightBrowse);

    vLayout->addWidget(pathBar);

    connect(m_leftBrowse,    &QToolButton::clicked,      this, &FileDiffWidget::onBrowseLeft);
    connect(m_rightBrowse,   &QToolButton::clicked,      this, &FileDiffWidget::onBrowseRight);
    connect(m_leftPathEdit,  &QLineEdit::returnPressed,  this, &FileDiffWidget::reloadFromPathBar);
    connect(m_rightPathEdit, &QLineEdit::returnPressed,  this, &FileDiffWidget::reloadFromPathBar);

    // Editors
    m_leftEditor  = new DiffEditor(Side::Left);
    m_rightEditor = new DiffEditor(Side::Right);
    m_splitter = new DiffConnectorSplitter(m_leftEditor, m_rightEditor, m_model, this);
    vLayout->addWidget(m_splitter, 1);
    m_unifiedEditor = new DiffEditor(Side::Right, this);
    vLayout->addWidget(m_unifiedEditor, 1);
    m_unifiedEditor->hide();
    for (auto* editor : {m_leftEditor, m_rightEditor, m_unifiedEditor}) {
        connect(editor, &DiffEditor::foldClicked, this, [this](int start) {
            m_openedFolds.insert(start); rebuildProjection();
        });
    }
    connect(m_unifiedEditor->edit()->area(), &qce::CodeEditArea::cursorPositionChanged, this, [this] {
        if (m_navigating) return;
        m_currentHunk = -1; updateNavLabel();
    });

    qce::CodeEdit* leftEdit  = m_leftEditor->edit();
    qce::CodeEdit* rightEdit = m_rightEditor->edit();

    for (auto* editor : {m_leftEditor, m_rightEditor}) {
        auto* area = editor->edit()->area();
        auto* bar = area->horizontalScrollBar();
        connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
            if (m_syncingHorizontal) return;
            m_horizontalOffset = value;
            QScopedValueRollback<bool> syncing(m_syncingHorizontal, true);
            auto* other = (bar == m_leftEditor->edit()->area()->horizontalScrollBar()
                          ? m_rightEditor : m_leftEditor)->edit()->area()->horizontalScrollBar();
            other->setValue(value);
        });
        connect(bar, &QScrollBar::rangeChanged, this, [this] { updateHorizontalScrollRange(); });
        connect(area, &qce::CodeEditArea::viewportChanged, this, [this] { updateHorizontalScrollRange(); });
        auto* document = area->document();
        const auto updateColumns = [this, editor, document] {
            (editor == m_leftEditor ? m_leftColumns : m_rightColumns) = document->maxLineLength();
            updateHorizontalScrollRange();
        };
        connect(document, &qce::ITextDocument::documentReset, this, updateColumns);
        connect(document, &qce::ITextDocument::linesChanged, this, updateColumns);
        connect(document, &qce::ITextDocument::linesInserted, this, updateColumns);
        connect(document, &qce::ITextDocument::linesRemoved, this, updateColumns);
    }

    for (auto* edit : {leftEdit, rightEdit}) {
        connect(edit->area(), &qce::CodeEditArea::cursorPositionChanged,
                this, [this, edit] {
            if (m_navigating) return;
            m_navigationSide = edit == m_leftEditor->edit() ? Side::Left : Side::Right;
            m_currentHunk = -1;
            updateNavLabel();
        });
    }

    connect(leftEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [this, rightEdit, previous = leftEdit->area()->viewportState()](const qce::ViewportState& vp) mutable {
        // Horizontal scrolling publishes the same signal. Preserve independently
        // positioned block endpoints unless the vertical viewport actually changes.
        const bool verticalChanged = vp.firstVisibleLine != previous.firstVisibleLine
            || vp.lastVisibleLine != previous.lastVisibleLine
            || vp.contentOffsetY != previous.contentOffsetY
            || vp.lineHeight != previous.lineHeight
            || vp.viewportHeight != previous.viewportHeight;
        previous = vp; // Track guarded navigation and updates to the other pane too.
        if (!verticalChanged || m_syncingScroll || m_viewMode == ViewMode::Unified) return;
        m_syncingScroll = true;
        const int otherCount = rightEdit->area()->document()->lineCount();
        const int sourceAnchor = vp.firstVisibleLine + int(vp.visibleLineCount()*m_syncMapper.threshold());
        const int original = m_leftEditor->originalLine(sourceAnchor);
        const int target = m_rightEditor->displayLine(int(m_syncMapper.correspondingLine(Side::Left, original)));
        const int otherTop = std::clamp(target - int(rightEdit->area()->viewportState().visibleLineCount()*m_syncMapper.threshold()), 0, std::max(0, otherCount-1));
        rightEdit->area()->verticalScrollBar()->setValue(otherTop);
        m_syncingScroll = false;
    });

    connect(rightEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [this, leftEdit, previous = rightEdit->area()->viewportState()](const qce::ViewportState& vp) mutable {
        // Horizontal scrolling publishes the same signal. Preserve independently
        // positioned block endpoints unless the vertical viewport actually changes.
        const bool verticalChanged = vp.firstVisibleLine != previous.firstVisibleLine
            || vp.lastVisibleLine != previous.lastVisibleLine
            || vp.contentOffsetY != previous.contentOffsetY
            || vp.lineHeight != previous.lineHeight
            || vp.viewportHeight != previous.viewportHeight;
        previous = vp; // Track guarded navigation and updates to the other pane too.
        if (!verticalChanged || m_syncingScroll || m_viewMode == ViewMode::Unified) return;
        m_syncingScroll = true;
        const int otherCount = leftEdit->area()->document()->lineCount();
        const int sourceAnchor = vp.firstVisibleLine + int(vp.visibleLineCount()*m_syncMapper.threshold());
        const int original = m_rightEditor->originalLine(sourceAnchor);
        const int target = m_leftEditor->displayLine(int(m_syncMapper.correspondingLine(Side::Right, original)));
        const int otherTop = std::clamp(target - int(leftEdit->area()->viewportState().visibleLineCount()*m_syncMapper.threshold()), 0, std::max(0, otherCount-1));
        leftEdit->area()->verticalScrollBar()->setValue(otherTop);
        m_syncingScroll = false;
    });
}

void FileDiffWidget::setContent(const QStringList& leftLines,
                                const QStringList& rightLines,
                                const diffcore::DiffOptions& opts) {
    ComparisonOptions options;
    options.diff = opts;
    TextSnapshot left, right;
    left.lines = leftLines;
    right.lines = rightLines;
    auto result = prepareComparison(left, right, options);
    if (result.status == PreparationStatus::Ready) setComparison(std::move(result.comparison));
    else emit loadFailed(result.message);
}

void FileDiffWidget::setComparison(std::shared_ptr<const PreparedComparison> comparison) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto start = std::chrono::steady_clock::now();
    // Keep the old result alive until documents, painters and callbacks detach.
    const auto previous = std::move(m_comparison);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    QScopedValueRollback<bool> navigating(m_navigating, true);
    clearSearchHighlights();
    m_splitter->setModel(nullptr);
    m_leftEditor->setIntraLineDiffs({});
    m_rightEditor->setIntraLineDiffs({});
    m_comparison = std::move(comparison);
    m_model = m_comparison ? &m_comparison->model() : nullptr;
    const double threshold = m_syncMapper.threshold();
    m_syncMapper = m_comparison ? m_comparison->scrollMapping() : ScrollSyncMapper{};
    m_syncMapper.setThreshold(threshold);
    m_leftEditor->setAlignedModel(m_model);
    m_rightEditor->setAlignedModel(m_model);
    if (m_comparison) {
        m_leftEditor->setIntraLineDiffs(m_comparison->highlights().leftRanges);
        m_rightEditor->setIntraLineDiffs(m_comparison->highlights().rightRanges);
    }
    m_leftEditor->setSyntaxFileName(m_comparison ? m_comparison->snapshot(Side::Left).fileName : QString{});
    m_rightEditor->setSyntaxFileName(m_comparison ? m_comparison->snapshot(Side::Right).fileName : QString{});
    m_splitter->setModel(m_model);
    m_openedFolds.clear();
    m_horizontalOffset = 0;
    m_leftEditor->edit()->area()->horizontalScrollBar()->setValue(0);
    m_rightEditor->edit()->area()->horizontalScrollBar()->setValue(0);
    rebuildProjection();
    m_currentHunk = -1;
    m_navigationSide = Side::Left;
    updateNavLabel();
    m_installationTime = std::chrono::steady_clock::now() - start;
    emit comparisonChanged(changeCount());
}


void FileDiffWidget::navigateToNext() {
    const auto& blocks = changes();
    if (blocks.isEmpty()) return;
    if (m_currentHunk >= 0) {
        navigateToHunk(m_currentHunk + 1);
        return;
    }

    const Side side = m_rightEditor->edit()->area()->hasFocus() ? Side::Right
                    : m_leftEditor->edit()->area()->hasFocus() ? Side::Left : m_navigationSide;
    const auto* area = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    int cursorLine = (side == Side::Left ? m_leftEditor : m_rightEditor)->originalLine(area->cursorPosition().line);
    if (m_viewMode == ViewMode::Unified) {
        const int row = m_unifiedEditor->edit()->area()->cursorPosition().line;
        if (row >= 0 && row < m_projection.rows().size()) {
            const auto& r = m_projection.rows()[row];
            cursorLine = side == Side::Left ? r.leftLine : r.rightLine;
            if (cursorLine < 0) cursorLine = int(m_syncMapper.correspondingLine(side == Side::Left ? Side::Right : Side::Left,
                side == Side::Left ? r.rightLine : r.leftLine));
        }
    }
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].range(side).start >= cursorLine) {
            navigateToHunk(i);
            return;
        }
    }
}

void FileDiffWidget::navigateToPrev() {
    const auto& blocks = changes();
    if (blocks.isEmpty()) return;
    if (m_currentHunk >= 0) {
        navigateToHunk(m_currentHunk - 1);
        return;
    }

    const Side side = m_rightEditor->edit()->area()->hasFocus() ? Side::Right
                    : m_leftEditor->edit()->area()->hasFocus() ? Side::Left : m_navigationSide;
    const auto* area = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    int cursorLine = (side == Side::Left ? m_leftEditor : m_rightEditor)->originalLine(area->cursorPosition().line);
    if (m_viewMode == ViewMode::Unified) {
        const int row = m_unifiedEditor->edit()->area()->cursorPosition().line;
        if (row >= 0 && row < m_projection.rows().size()) {
            const auto& r = m_projection.rows()[row];
            cursorLine = side == Side::Left ? r.leftLine : r.rightLine;
            if (cursorLine < 0) cursorLine = int(m_syncMapper.correspondingLine(side == Side::Left ? Side::Right : Side::Left,
                side == Side::Left ? r.rightLine : r.leftLine));
        }
    }
    for (int i = blocks.size() - 1; i >= 0; --i) {
        if (blocks[i].range(side).start <= cursorLine) {
            navigateToHunk(i);
            return;
        }
    }
}

void FileDiffWidget::navigateToHunk(int idx) {
    const auto& blocks = changes();
    if (idx < 0 || idx >= blocks.size()) return;
    QScopedValueRollback<bool> navigating(m_navigating, true);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);

    m_currentHunk = idx;
    const auto& block = blocks[idx];
    const int hunkSpan = std::max(block.leftRange.count, block.rightRange.count);

    const int leftDoc  = m_leftEditor->displayLine(block.leftRange.start);
    const int rightDoc = m_rightEditor->displayLine(block.rightRange.start);
    if (m_viewMode == ViewMode::Unified) {
        const Side side = block.leftRange.count ? Side::Left : Side::Right;
        const int row = unifiedLine(side, block.range(side).start);
        auto* area = m_unifiedEditor->edit()->area();
        area->setCursorPosition({row, 0});
        area->verticalScrollBar()->setValue(std::max(0, row-int(area->viewportState().visibleLineCount()*0.4)));
        updateNavLabel(); return;
    }

    // Place hunk at ~40% from top; reduce to 20% for large hunks
    const int visible = m_leftEditor->edit()->area()->viewportState().visibleLineCount();
    const double fraction = (visible > 0 && hunkSpan > visible * 0.3) ? 0.2 : 0.4;
    const int leftOffset = static_cast<int>(visible * fraction);
    const int rightOffset = static_cast<int>(
        m_rightEditor->edit()->area()->viewportState().visibleLineCount() * fraction);

    m_leftEditor->edit()->area()->verticalScrollBar()->setValue(std::max(0, leftDoc  - leftOffset));
    m_rightEditor->edit()->area()->verticalScrollBar()->setValue(std::max(0, rightDoc - rightOffset));

    // Move caret to the selected change for subsequent host navigation.
    const int leftDocCount  = m_leftEditor->edit()->area()->document()->lineCount();
    const int rightDocCount = m_rightEditor->edit()->area()->document()->lineCount();
    m_leftEditor->edit()->area()->setCursorPosition(
        {std::min(leftDoc,  std::max(0, leftDocCount  - 1)), 0});
    m_rightEditor->edit()->area()->setCursorPosition(
        {std::min(rightDoc, std::max(0, rightDocCount - 1)), 0});

    updateNavLabel();
}

void FileDiffWidget::updateNavLabel() {
    const int total = changeCount();
    if (total == 0) {
        m_navLabel->setText(QStringLiteral("No changes"));
    } else if (m_currentHunk < 0) {
        m_navLabel->setText(QStringLiteral("%1 change(s)").arg(total));
    } else {
        m_navLabel->setText(QStringLiteral("%1 / %2").arg(m_currentHunk + 1).arg(total));
    }
    m_prevButton->setEnabled(total > 0);
    m_nextButton->setEnabled(total > 0);
    if (m_notifiedChange != m_currentHunk) {
        m_notifiedChange = m_currentHunk;
        emit currentChangeChanged(m_currentHunk);
    }

}


const QVector<ChangeBlock>& FileDiffWidget::changes() const {
    static const QVector<ChangeBlock> empty;
    return m_comparison ? m_comparison->changes() : empty;
}

int FileDiffWidget::changeCount() const { return changes().size(); }

bool FileDiffWidget::navigateToChange(int index) {
    if (index < 0 || index >= changeCount()) return false;
    navigateToHunk(index);
    return true;
}

bool FileDiffWidget::revealLines(Side side, diffcore::LineRange range, bool emphasize) {
    if (!m_comparison) return false;
    const int count = m_comparison->snapshot(side).lines.size();
    if (range.start < 0 || range.start > count || range.count < 0 || range.count > count - range.start)
        return false;
    QScopedValueRollback<bool> navigating(m_navigating, true);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    openContaining(side, range);
    auto* area = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    auto* other = (side == Side::Left ? m_rightEditor : m_leftEditor)->edit()->area();
    const int mapped = static_cast<int>(m_syncMapper.correspondingLine(side, range.start));
    const auto position = [](qce::CodeEditArea* pane, int line) {
        pane->verticalScrollBar()->setValue(std::max(0, line - static_cast<int>(pane->viewportState().visibleLineCount() * 0.4)));
        pane->setCursorPosition({std::min(line, std::max(0, pane->document()->lineCount() - 1)), 0});
    };
    if (m_viewMode == ViewMode::Unified) position(m_unifiedEditor->edit()->area(), unifiedLine(side, range.start));
    else {
        position(area, (side == Side::Left ? m_leftEditor : m_rightEditor)->displayLine(range.start));
        position(other, (side == Side::Left ? m_rightEditor : m_leftEditor)->displayLine(mapped));
    }
    m_navigationSide = side;
    m_currentHunk = -1;
    m_emphasis = emphasize ? std::make_optional(std::make_pair(side, range)) : std::nullopt;
    refreshSearchHighlights();
    updateNavLabel();
    return true;
}

bool FileDiffWidget::validTextRange(Side side, TextRange range) const {
    if (!m_comparison || range.line < 0 || range.column < 0 || range.length < 0) return false;
    const auto& lines = m_comparison->snapshot(side).lines;
    if (range.line == lines.size()) return range.column == 0 && range.length == 0;
    if (range.line > lines.size()) return false;
    const auto size = lines[range.line].size();
    return range.column <= size && range.length <= size - range.column;
}

bool FileDiffWidget::revealText(Side side, TextRange range, bool emphasize) {
    if (!validTextRange(side, range)) return false;
    const bool atEnd = range.line == m_comparison->snapshot(side).lines.size();
    if (!revealLines(side, {range.line, range.length == 0 ? 0 : 1}, emphasize)) return false;
    if (!atEnd) {
        QScopedValueRollback<bool> navigating(m_navigating, true);
        auto* editor = side == Side::Left ? m_leftEditor : m_rightEditor;
        if (m_viewMode == ViewMode::Unified) m_unifiedEditor->edit()->area()->setCursorPosition({unifiedLine(side, range.line), range.column});
        else editor->edit()->area()->setCursorPosition({editor->displayLine(range.line), range.column});
    }
    return true;
}

bool FileDiffWidget::setSearchHighlights(Side side, const QVector<TextRange>& ranges) {
    for (const auto& range : ranges) if (!validTextRange(side, range)) return false;
    if (!m_comparison) return false;
    for (const auto& range : ranges) openContaining(side, {range.line, 1});
    (side == Side::Left ? m_leftSearch : m_rightSearch) = ranges;
    refreshSearchHighlights();
    return true;
}

void FileDiffWidget::clearSearchHighlights() {
    m_leftSearch.clear();
    m_rightSearch.clear();
    m_emphasis.reset();
    refreshSearchHighlights();
}

void FileDiffWidget::setSyntaxFileName(Side side, const QString& fileName) {
    (side == Side::Left ? m_leftEditor : m_rightEditor)->setSyntaxFileName(fileName);
    rebuildProjection();
}

void FileDiffWidget::reloadSyntaxDefinitions() {
    m_leftEditor->reloadSyntaxDefinitions();
    m_rightEditor->reloadSyntaxDefinitions();
    rebuildProjection();
}

void FileDiffWidget::refreshSearchHighlights() {
    QVector<qce::ExtraSelection> unifiedSelections;
    QVector<int> unifiedBoundaries;
    for (Side side : {Side::Left, Side::Right}) {
        auto* editor = side == Side::Left ? m_leftEditor : m_rightEditor;
        if (!editor) continue;
        QVector<qce::ExtraSelection> selections;
        QVector<int> boundaries;
        const auto& ranges = side == Side::Left ? m_leftSearch : m_rightSearch;
        for (const auto& range : ranges) {
            const int row = editor->displayLine(range.line);
            const int unifiedRow = unifiedLine(side, range.line);
            if (!range.length) { boundaries.append(row); unifiedBoundaries.append(unifiedRow); continue; }
            qce::ExtraSelection selection;
            selection.start = {row, range.column};
            selection.end = {row, range.column + range.length};
            selection.background = QColor(255, 210, 40, 120);
            selections.append(selection);
            selection.start.line = unifiedRow; selection.end.line = unifiedRow;
            unifiedSelections.append(selection);
        }
        editor->edit()->area()->setExtraSelections(selections);
        std::optional<diffcore::LineRange> emphasis;
        if (m_emphasis && m_emphasis->first == side) {
            const auto r = m_emphasis->second;
            emphasis = {editor->displayLine(r.start), editor->displayLine(r.end())-editor->displayLine(r.start)};
        }
        editor->setRevealOverlay(emphasis, boundaries);
    }
    if (m_unifiedEditor) {
        m_unifiedEditor->edit()->area()->setExtraSelections(unifiedSelections);
        std::optional<diffcore::LineRange> emphasis;
        if (m_emphasis) {
            const auto [side, r] = *m_emphasis;
            const int first = unifiedLine(side, r.start);
            const int last = r.count ? unifiedLine(side, r.end()-1)+1 : first;
            emphasis = {first, last-first};
        }
        m_unifiedEditor->setRevealOverlay(emphasis, unifiedBoundaries);
    }
}

void FileDiffWidget::setPaths(const QString& leftPath, const QString& rightPath) {
    m_leftPathEdit->setText(leftPath);
    m_rightPathEdit->setText(rightPath);
}

bool FileDiffWidget::loadFromPaths(const QString& leftPath,
                                   const QString& rightPath) {
    auto readFile = [&](const QString& path, TextSnapshot& out) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            emit loadFailed(QStringLiteral("Cannot open %1: %2").arg(path, f.errorString()));
            return false;
        }
        QTextStream in(&f);
        try {
            out = TextSnapshot::fromText(in.readAll(), path, path);
            return true;
        } catch (const std::exception& error) {
            emit loadFailed(QString::fromUtf8(error.what()));
            return false;
        }
    };

    TextSnapshot left, right;
    if (!readFile(leftPath, left)) return false;
    if (!readFile(rightPath, right)) return false;

    auto result = prepareComparison(left, right);
    if (result.status != PreparationStatus::Ready) { emit loadFailed(result.message); return false; }
    m_leftPathEdit->setText(leftPath);
    m_rightPathEdit->setText(rightPath);
    setComparison(std::move(result.comparison));
    emit pathsChanged(leftPath, rightPath);
    return true;
}

void FileDiffWidget::onBrowseLeft() {
    emit fileBrowseRequested(Side::Left, m_leftPathEdit->text());
}

void FileDiffWidget::onBrowseRight() {
    emit fileBrowseRequested(Side::Right, m_rightPathEdit->text());
}

void FileDiffWidget::setPath(Side side, const QString& path) {
    (side == Side::Left ? m_leftPathEdit : m_rightPathEdit)->setText(path);
    reloadFromPathBar();
}

void FileDiffWidget::reloadFromPathBar() {
    const QString l = m_leftPathEdit->text().trimmed();
    const QString r = m_rightPathEdit->text().trimmed();
    if (l.isEmpty() || r.isEmpty()) return;
    loadFromPaths(l, r);
}

void FileDiffWidget::setBackVisible(bool visible) {
    m_backButton->setVisible(visible);
    m_backSep->setVisible(visible);
}

void FileDiffWidget::setSyncThreshold(double fraction) {
    m_syncMapper.setThreshold(fraction);
}

double FileDiffWidget::syncThreshold() const {
    return m_syncMapper.threshold();
}


void FileDiffWidget::updateHorizontalScrollRange() {
    if (m_syncingHorizontal) return;
    QScopedValueRollback<bool> syncing(m_syncingHorizontal, true);
    const auto maximum = [](DiffEditor* editor, int columns) {
        const auto* area = editor->edit()->area();
        const int charWidth = QFontMetrics(area->font()).horizontalAdvance(QLatin1Char('M'));
        const int visibleColumns = charWidth > 0 ? area->viewport()->width() / charWidth : 0;
        return std::max(0, columns - visibleColumns);
    };
    const int sharedMaximum = std::max(maximum(m_leftEditor, m_leftColumns),
                                       maximum(m_rightEditor, m_rightColumns));
    if (m_leftEditor->edit()->area()->horizontalScrollBar()->maximum() == sharedMaximum &&
        m_rightEditor->edit()->area()->horizontalScrollBar()->maximum() == sharedMaximum)
        return; // A value change publishes its viewport before valueChanged reaches us.
    m_horizontalOffset = std::clamp(m_horizontalOffset, 0, sharedMaximum);
    for (auto* editor : {m_leftEditor, m_rightEditor}) {
        auto* bar = editor->edit()->area()->horizontalScrollBar();
        // qcodeedit recalculates its own range on document/viewport changes.
        // Give both panes the union so short lines can move with long ones.
        bar->setRange(0, sharedMaximum);
        bar->setValue(m_horizontalOffset);
    }
}

int FileDiffWidget::unifiedLine(Side side, int line) const { return m_projection.rowFor(side, line); }
void FileDiffWidget::setViewMode(ViewMode mode) {
    if (m_viewMode == mode) return;
    Side anchorSide = Side::Right;
    int anchor = 0;
    if (m_viewMode == ViewMode::Unified) {
        const int row = m_unifiedEditor->edit()->area()->viewportState().firstVisibleLine;
        if (row >= 0 && row < m_projection.rows().size()) {
            const auto& r = m_projection.rows()[row];
            anchorSide = r.rightLine >= 0 ? Side::Right : Side::Left;
            anchor = anchorSide == Side::Right ? r.rightLine : r.leftLine;
        }
    } else {
        anchorSide = m_leftEditor->edit()->area()->hasFocus() ? Side::Left : Side::Right;
        const auto* editor = anchorSide == Side::Left ? m_leftEditor : m_rightEditor;
        anchor = editor->originalLine(editor->edit()->area()->viewportState().firstVisibleLine);
    }
    m_viewMode = mode;
    rebuildProjection();
    layout()->activate();
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    if (mode == ViewMode::Unified) m_unifiedEditor->edit()->area()->verticalScrollBar()->setValue(unifiedLine(anchorSide, anchor));
    else {
        const int other = int(m_syncMapper.correspondingLine(anchorSide, anchor));
        m_leftEditor->edit()->area()->verticalScrollBar()->setValue(m_leftEditor->displayLine(anchorSide == Side::Left ? anchor : other));
        m_rightEditor->edit()->area()->verticalScrollBar()->setValue(m_rightEditor->displayLine(anchorSide == Side::Right ? anchor : other));
    }
    emit viewModeChanged(mode);
}
void FileDiffWidget::setUnchangedLinesSkipped(bool skipped) {
    if (m_skipUnchanged == skipped) return;
    m_skipUnchanged = skipped; m_openedFolds.clear(); rebuildProjection();
    emit unchangedLinesSkippedChanged(skipped);
}
void FileDiffWidget::setContextLines(int lines) {
    lines = std::max(0, lines);
    if (m_contextLines == lines) return;
    m_contextLines = lines; m_openedFolds.clear(); rebuildProjection();
}
void FileDiffWidget::openContaining(Side side, diffcore::LineRange range) {
    bool changed = false;
    for (const auto& row : m_projection.rows()) {
        if (!row.hiddenCount) continue;
        const int first = side == Side::Left ? row.leftLine : row.rightLine;
        if (range.start < first+row.hiddenCount && range.start+std::max(1, range.count) > first) {
            m_openedFolds.insert(row.leftLine); changed = true;
        }
    }
    if (changed) rebuildProjection();
}
void FileDiffWidget::rebuildProjection() {
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);
    QScopedValueRollback<bool> navigating(m_navigating, true);
    const int leftAnchor = m_leftEditor->originalLine(m_leftEditor->edit()->area()->viewportState().firstVisibleLine);
    const int rightAnchor = m_rightEditor->originalLine(m_rightEditor->edit()->area()->viewportState().firstVisibleLine);
    const int unifiedTop = m_unifiedEditor->edit()->area()->viewportState().firstVisibleLine;
    Side unifiedSide = Side::Right;
    int unifiedAnchor = rightAnchor;
    if (unifiedTop >= 0 && unifiedTop < m_projection.rows().size()) {
        const auto& r = m_projection.rows()[unifiedTop];
        unifiedSide = r.rightLine >= 0 ? Side::Right : Side::Left;
        unifiedAnchor = unifiedSide == Side::Right ? r.rightLine : r.leftLine;
    }
    m_splitter->setVisible(m_viewMode == ViewMode::SideBySide);
    m_unifiedEditor->setVisible(m_viewMode == ViewMode::Unified);
    if (!m_comparison) {
        m_projection = {};
        m_unifiedEditor->setAlignedModel(nullptr); return;
    }
    m_projection.build(*m_comparison, m_skipUnchanged, m_contextLines, m_openedFolds);
    // Persistent host highlights must never land on placeholder text after
    // changing context or enabling skipping.
    bool opened = false;
    for (const auto& row : m_projection.rows()) {
        if (!row.hiddenCount) continue;
        const auto overlaps = [&](Side side, diffcore::LineRange range) {
            const int first = side == Side::Left ? row.leftLine : row.rightLine;
            return range.start < first+row.hiddenCount && range.start+std::max(1, range.count) > first;
        };
        bool needed = m_emphasis && overlaps(m_emphasis->first, m_emphasis->second);
        for (const auto& range : m_leftSearch) needed |= overlaps(Side::Left, {range.line, 1});
        for (const auto& range : m_rightSearch) needed |= overlaps(Side::Right, {range.line, 1});
        if (needed) { m_openedFolds.insert(row.leftLine); opened = true; }
    }
    if (opened) m_projection.build(*m_comparison, m_skipUnchanged, m_contextLines, m_openedFolds);
    if (m_viewMode == ViewMode::Unified) {
        m_unifiedEditor->setSyntaxFileName(m_rightEditor->syntaxFileName());
        m_unifiedEditor->setProjection(m_comparison, m_projection.rows(), true, m_leftEditor->syntaxFileName());
    } else m_unifiedEditor->setAlignedModel(nullptr);
    for (Side side : {Side::Left, Side::Right}) {
        auto* editor = side == Side::Left ? m_leftEditor : m_rightEditor;
        if (m_skipUnchanged) {
            QVector<ViewRow> rows;
            for (const auto& row : m_projection.rows()) if ((side == Side::Left ? row.leftLine : row.rightLine) >= 0) rows.append(row);
            editor->setProjection(m_comparison, rows, false);
        } else if (editor->hasProjection()) {
            editor->setAlignedModel(m_model);
            editor->setIntraLineDiffs(side == Side::Left ? m_comparison->highlights().leftRanges : m_comparison->highlights().rightRanges);
            editor->reloadSyntaxDefinitions();
        }
    }
    m_leftEditor->edit()->area()->verticalScrollBar()->setValue(m_leftEditor->displayLine(leftAnchor));
    m_rightEditor->edit()->area()->verticalScrollBar()->setValue(m_rightEditor->displayLine(rightAnchor));
    m_unifiedEditor->edit()->area()->verticalScrollBar()->setValue(unifiedLine(unifiedSide, unifiedAnchor));
    updateHorizontalScrollRange();
    refreshSearchHighlights(); m_splitter->updateConnections();
}

}  // namespace diffmerge::gui
