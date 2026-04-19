#include "FileDiffWidget.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>
#include <qce/ViewportState.h>

#include "../editor/DiffEditor.h"
#include "../editor/IntraLineDiffEngine.h"

namespace diffmerge::gui {

FileDiffWidget::FileDiffWidget(QWidget* parent)
    : QWidget(parent), m_model(std::make_unique<AlignedLineModel>()) {
    setupUi();
}

FileDiffWidget::~FileDiffWidget() = default;

void FileDiffWidget::setupUi() {
    auto* vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    // Navigation toolbar
    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    m_backButton = new QToolButton(toolbar);
    m_backButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogBack));
    m_backButton->setText(QStringLiteral("← Directories"));
    m_backButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_backButton->setToolTip(QStringLiteral("Back to directory view"));
    m_backButton->setVisible(false);
    toolbar->addWidget(m_backButton);
    toolbar->addSeparator();

    connect(m_backButton, &QToolButton::clicked, this, &FileDiffWidget::backRequested);

    m_prevButton = new QToolButton(toolbar);
    m_prevButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_prevButton->setToolTip(QStringLiteral("Previous change (Shift+F7)"));
    toolbar->addWidget(m_prevButton);

    m_nextButton = new QToolButton(toolbar);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_nextButton->setToolTip(QStringLiteral("Next change (F7)"));
    toolbar->addWidget(m_nextButton);

    m_navLabel = new QLabel(QStringLiteral("No changes"), toolbar);
    m_navLabel->setMargin(4);
    toolbar->addWidget(m_navLabel);

    vLayout->addWidget(toolbar);

    connect(m_prevButton, &QToolButton::clicked, this, &FileDiffWidget::navigateToPrev);
    connect(m_nextButton, &QToolButton::clicked, this, &FileDiffWidget::navigateToNext);

    auto* nextShortcut = new QShortcut(Qt::Key_F7, this);
    connect(nextShortcut, &QShortcut::activated, this, &FileDiffWidget::navigateToNext);

    auto* prevShortcut = new QShortcut(Qt::ShiftModifier | Qt::Key_F7, this);
    connect(prevShortcut, &QShortcut::activated, this, &FileDiffWidget::navigateToPrev);

    // Editors
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_leftEditor  = new DiffEditor(Side::Left,  splitter);
    m_rightEditor = new DiffEditor(Side::Right, splitter);
    splitter->addWidget(m_leftEditor);
    splitter->addWidget(m_rightEditor);
    splitter->setSizes({1, 1});
    splitter->setChildrenCollapsible(false);
    vLayout->addWidget(splitter);

    qce::CodeEdit* leftEdit  = m_leftEditor->edit();
    qce::CodeEdit* rightEdit = m_rightEditor->edit();

    connect(leftEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [this, rightEdit](const qce::ViewportState& vp) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        const int otherCount = rightEdit->area()->document()->lineCount();
        const int otherTop   = m_syncMapper.computeOtherTop(
            Side::Left, vp.firstVisibleLine, vp.visibleLineCount(), otherCount);
        rightEdit->area()->verticalScrollBar()->setValue(otherTop);
        m_syncingScroll = false;
    });

    connect(rightEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [this, leftEdit](const qce::ViewportState& vp) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        const int otherCount = leftEdit->area()->document()->lineCount();
        const int otherTop   = m_syncMapper.computeOtherTop(
            Side::Right, vp.firstVisibleLine, vp.visibleLineCount(), otherCount);
        leftEdit->area()->verticalScrollBar()->setValue(otherTop);
        m_syncingScroll = false;
    });
}

void FileDiffWidget::setContent(const QStringList& leftLines,
                                const QStringList& rightLines,
                                const diffcore::DiffOptions& opts) {
    diffcore::DiffEngine engine;
    const diffcore::DiffResult result = engine.compute(leftLines, rightLines, opts);
    m_model->build(result, leftLines, rightLines);
    m_syncMapper.build(result);

    m_leftEditor->setAlignedModel(m_model.get());
    m_rightEditor->setAlignedModel(m_model.get());

    const auto charDiff = IntraLineDiffEngine::compute(result, leftLines, rightLines);
    m_leftEditor->setIntraLineDiffs(charDiff.leftRanges);
    m_rightEditor->setIntraLineDiffs(charDiff.rightRanges);

    m_currentHunk = -1;
    updateNavLabel();
}

void FileDiffWidget::navigateToNext() {
    const QVector<int>& starts = m_model->hunkAlignedStarts();
    if (starts.isEmpty()) return;

    const int cursorLine = m_leftEditor->edit()->area()->cursorPosition().line;
    for (int i = 0; i < starts.size(); ++i) {
        const int hunkDocLine = m_model->docLineBeforeAligned(Side::Left, starts[i]);
        if (hunkDocLine > cursorLine) {
            navigateToHunk(i);
            return;
        }
    }
}

void FileDiffWidget::navigateToPrev() {
    const QVector<int>& starts = m_model->hunkAlignedStarts();
    if (starts.isEmpty()) return;

    const int cursorLine = m_leftEditor->edit()->area()->cursorPosition().line;
    for (int i = starts.size() - 1; i >= 0; --i) {
        const int hunkDocLine = m_model->docLineBeforeAligned(Side::Left, starts[i]);
        if (hunkDocLine < cursorLine) {
            navigateToHunk(i);
            return;
        }
    }
}

void FileDiffWidget::navigateToHunk(int idx) {
    const QVector<int>& starts = m_model->hunkAlignedStarts();
    const QVector<int>& ends   = m_model->hunkAlignedEnds();
    if (idx < 0 || idx >= starts.size()) return;

    m_currentHunk = idx;
    const int alignedRow = starts[idx];
    const int hunkSpan   = ends[idx] - alignedRow;

    const int leftDoc  = m_model->docLineBeforeAligned(Side::Left,  alignedRow);
    const int rightDoc = m_model->docLineBeforeAligned(Side::Right, alignedRow);

    // Place hunk at ~40% from top; reduce to 20% for large hunks
    const int visible = m_leftEditor->edit()->area()->viewportState().visibleLineCount();
    const double fraction = (visible > 0 && hunkSpan > visible * 0.3) ? 0.2 : 0.4;
    const int offset = static_cast<int>(visible * fraction);

    m_syncingScroll = true;
    m_leftEditor->edit()->area()->verticalScrollBar()->setValue(std::max(0, leftDoc  - offset));
    m_rightEditor->edit()->area()->verticalScrollBar()->setValue(std::max(0, rightDoc - offset));
    m_syncingScroll = false;

    // Move caret to hunk start so next F7/Shift+F7 is relative to it
    const int leftDocCount  = m_leftEditor->edit()->area()->document()->lineCount();
    const int rightDocCount = m_rightEditor->edit()->area()->document()->lineCount();
    m_leftEditor->edit()->area()->setCursorPosition(
        {std::min(leftDoc,  std::max(0, leftDocCount  - 1)), 0});
    m_rightEditor->edit()->area()->setCursorPosition(
        {std::min(rightDoc, std::max(0, rightDocCount - 1)), 0});

    updateNavLabel();
}

void FileDiffWidget::updateNavLabel() {
    const int total = m_model->hunkAlignedStarts().size();
    if (total == 0) {
        m_navLabel->setText(QStringLiteral("No changes"));
    } else if (m_currentHunk < 0) {
        m_navLabel->setText(QStringLiteral("%1 change(s)").arg(total));
    } else {
        m_navLabel->setText(QStringLiteral("%1 / %2").arg(m_currentHunk + 1).arg(total));
    }
    m_prevButton->setEnabled(total > 0);
    m_nextButton->setEnabled(total > 0);
}

void FileDiffWidget::setBackVisible(bool visible) {
    m_backButton->setVisible(visible);
}

void FileDiffWidget::setSyncThreshold(double fraction) {
    m_syncMapper.setThreshold(fraction);
}

double FileDiffWidget::syncThreshold() const {
    return m_syncMapper.threshold();
}

}  // namespace diffmerge::gui
