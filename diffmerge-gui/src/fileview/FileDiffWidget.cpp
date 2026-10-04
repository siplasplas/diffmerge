#include <diffmerge/FileDiffWidget.h>

#include <QFile>
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
    m_metadataLabel = new QLabel(this);
    m_metadataLabel->setObjectName(QStringLiteral("diffTextMetadata"));
    m_metadataLabel->setTextFormat(Qt::PlainText);
    m_metadataLabel->setMargin(4);
    m_metadataLabel->hide();
    vLayout->addWidget(m_metadataLabel);

    connect(m_leftBrowse,    &QToolButton::clicked,      this, &FileDiffWidget::onBrowseLeft);
    connect(m_rightBrowse,   &QToolButton::clicked,      this, &FileDiffWidget::onBrowseRight);
    connect(m_leftPathEdit,  &QLineEdit::returnPressed,  this, &FileDiffWidget::reloadFromPathBar);
    connect(m_rightPathEdit, &QLineEdit::returnPressed,  this, &FileDiffWidget::reloadFromPathBar);

    // Editors
    m_leftEditor  = new DiffEditor(Side::Left);
    m_rightEditor = new DiffEditor(Side::Right);
    m_splitter = new DiffConnectorSplitter(m_leftEditor, m_rightEditor, m_model, this);
    vLayout->addWidget(m_splitter);

    qce::CodeEdit* leftEdit  = m_leftEditor->edit();
    qce::CodeEdit* rightEdit = m_rightEditor->edit();

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
            this, [this, rightEdit](const qce::ViewportState& vp) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        const int otherCount = rightEdit->area()->document()->lineCount();
        const int otherTop   = m_syncMapper.computeOtherTop(
            Side::Left, vp.firstVisibleLine, vp.visibleLineCount(), otherCount,
            rightEdit->area()->viewportState().visibleLineCount());
        rightEdit->area()->verticalScrollBar()->setValue(otherTop);
        m_syncingScroll = false;
    });

    connect(rightEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [this, leftEdit](const qce::ViewportState& vp) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        const int otherCount = leftEdit->area()->document()->lineCount();
        const int otherTop   = m_syncMapper.computeOtherTop(
            Side::Right, vp.firstVisibleLine, vp.visibleLineCount(), otherCount,
            leftEdit->area()->viewportState().visibleLineCount());
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
    m_splitter->setModel(m_model);
    m_currentHunk = -1;
    m_navigationSide = Side::Left;
    updateMetadataLabel();
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
    const int cursorLine = area->cursorPosition().line;
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
    const int cursorLine = area->cursorPosition().line;
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

    const int leftDoc  = block.leftRange.start;
    const int rightDoc = block.rightRange.start;

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
    auto* area = (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area();
    auto* other = (side == Side::Left ? m_rightEditor : m_leftEditor)->edit()->area();
    const int mapped = static_cast<int>(m_syncMapper.correspondingLine(side, range.start));
    const auto position = [](qce::CodeEditArea* pane, int line) {
        pane->verticalScrollBar()->setValue(std::max(0, line - static_cast<int>(pane->viewportState().visibleLineCount() * 0.4)));
        pane->setCursorPosition({std::min(line, std::max(0, pane->document()->lineCount() - 1)), 0});
    };
    position(area, range.start);
    position(other, mapped);
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
        (side == Side::Left ? m_leftEditor : m_rightEditor)->edit()->area()->setCursorPosition({range.line, range.column});
    }
    return true;
}

bool FileDiffWidget::setSearchHighlights(Side side, const QVector<TextRange>& ranges) {
    for (const auto& range : ranges) if (!validTextRange(side, range)) return false;
    if (!m_comparison) return false;
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

void FileDiffWidget::refreshSearchHighlights() {
    for (Side side : {Side::Left, Side::Right}) {
        auto* editor = side == Side::Left ? m_leftEditor : m_rightEditor;
        if (!editor) continue;
        QVector<qce::ExtraSelection> selections;
        QVector<int> boundaries;
        const auto& ranges = side == Side::Left ? m_leftSearch : m_rightSearch;
        for (const auto& range : ranges) {
            if (!range.length) { boundaries.append(range.line); continue; }
            qce::ExtraSelection selection;
            selection.start = {range.line, range.column};
            selection.end = {range.line, range.column + range.length};
            selection.background = QColor(255, 210, 40, 120);
            selections.append(selection);
        }
        editor->edit()->area()->setExtraSelections(selections);
        editor->setRevealOverlay(m_emphasis && m_emphasis->first == side
            ? std::make_optional(m_emphasis->second) : std::nullopt, boundaries);
    }
}

void FileDiffWidget::updateMetadataLabel() {
    QStringList descriptions;
    if (m_comparison) for (Side side : {Side::Left, Side::Right}) {
        const auto& snapshot = m_comparison->snapshot(side);
        QStringList parts;
        if (!snapshot.label.isEmpty()) parts.append(snapshot.label);
        if (snapshot.finalNewline) {
            if (snapshot.lines.isEmpty()) parts.append(QStringLiteral("Empty file"));
            else parts.append(*snapshot.finalNewline ? QStringLiteral("Final newline") : QStringLiteral("No final newline"));
        }
        QStringList endings;
        for (auto ending : snapshot.lineEndings) {
            QString name;
            if (ending == LineEnding::LF) name = "LF";
            else if (ending == LineEnding::CRLF) name = "CRLF";
            else if (ending == LineEnding::CR) name = "CR";
            if (!name.isEmpty() && !endings.contains(name)) endings.append(name);
        }
        if (!endings.isEmpty()) parts.append(endings.join('/'));
        if (!parts.isEmpty()) descriptions.append((side == Side::Left ? QStringLiteral("Left: ") : QStringLiteral("Right: ")) + parts.join(" — "));
    }
    m_metadataLabel->setText(descriptions.join("    |    "));
    m_metadataLabel->setVisible(!descriptions.isEmpty());
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
            out = TextSnapshot::fromText(in.readAll(), path);
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

}  // namespace diffmerge::gui
