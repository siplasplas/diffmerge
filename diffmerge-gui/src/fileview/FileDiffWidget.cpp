#include "FileDiffWidget.h"

#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>
#include <qce/ViewportState.h>

#include "../editor/DiffEditor.h"
#include "../editor/IntraLineDiffEngine.h"
#include "DiffConnectorSplitter.h"

namespace diffmerge::gui {

FileDiffWidget::FileDiffWidget(QWidget* parent)
    : QWidget(parent), m_model(std::make_unique<AlignedLineModel>()) {
    setupUi();
}

FileDiffWidget::~FileDiffWidget() {
    // The divider reads the model while painting; destroy it before the model.
    delete m_splitter;
}

void FileDiffWidget::setupUi() {
    auto* vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    // Navigation bar
    auto* navBar    = new QWidget(this);
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
    m_prevButton->setToolTip(QStringLiteral("Previous change (Shift+F7)"));
    m_prevButton->setAutoRaise(true);
    navLayout->addWidget(m_prevButton);

    m_nextButton = new QToolButton(navBar);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_nextButton->setToolTip(QStringLiteral("Next change (F7)"));
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

    auto* nextShortcut = new QShortcut(Qt::Key_F7, this);
    connect(nextShortcut, &QShortcut::activated, this, &FileDiffWidget::navigateToNext);

    auto* prevShortcut = new QShortcut(Qt::ShiftModifier | Qt::Key_F7, this);
    connect(prevShortcut, &QShortcut::activated, this, &FileDiffWidget::navigateToPrev);

    // Path bar
    auto* pathBar    = new QWidget(this);
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
    m_splitter = new DiffConnectorSplitter(m_leftEditor, m_rightEditor, m_model.get(), this);
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
    diffcore::DiffEngine engine;
    const diffcore::DiffResult result = engine.compute(leftLines, rightLines, opts);
    m_model->build(result, leftLines, rightLines);
    m_syncMapper.build(result);
    QScopedValueRollback<bool> syncing(m_syncingScroll, true);

    m_leftEditor->setAlignedModel(m_model.get());
    m_rightEditor->setAlignedModel(m_model.get());

    const auto charDiff = IntraLineDiffEngine::compute(result, leftLines, rightLines);
    m_leftEditor->setIntraLineDiffs(charDiff.leftRanges);
    m_rightEditor->setIntraLineDiffs(charDiff.rightRanges);
    m_splitter->updateConnections();

    m_currentHunk = -1;
    m_navigationSide = Side::Left;
    updateNavLabel();
}

void FileDiffWidget::navigateToNext() {
    const auto& blocks = m_model->changeBlocks();
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
    const auto& blocks = m_model->changeBlocks();
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
    const auto& blocks = m_model->changeBlocks();
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

void FileDiffWidget::setPaths(const QString& leftPath, const QString& rightPath) {
    m_leftPathEdit->setText(leftPath);
    m_rightPathEdit->setText(rightPath);
}

bool FileDiffWidget::loadFromPaths(const QString& leftPath,
                                   const QString& rightPath) {
    auto readFile = [&](const QString& path, QStringList& out) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, QStringLiteral("Error"),
                QStringLiteral("Cannot open %1: %2").arg(path, f.errorString()));
            return false;
        }
        QTextStream in(&f);
        while (!in.atEnd()) out.append(in.readLine());
        return true;
    };

    QStringList leftLines, rightLines;
    if (!readFile(leftPath, leftLines)) return false;
    if (!readFile(rightPath, rightLines)) return false;

    m_leftPathEdit->setText(leftPath);
    m_rightPathEdit->setText(rightPath);
    setContent(leftLines, rightLines);
    emit pathsChanged(leftPath, rightPath);
    return true;
}

void FileDiffWidget::onBrowseLeft() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select left file"), m_leftPathEdit->text());
    if (!path.isEmpty()) {
        m_leftPathEdit->setText(path);
        reloadFromPathBar();
    }
}

void FileDiffWidget::onBrowseRight() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select right file"), m_rightPathEdit->text());
    if (!path.isEmpty()) {
        m_rightPathEdit->setText(path);
        reloadFromPathBar();
    }
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
