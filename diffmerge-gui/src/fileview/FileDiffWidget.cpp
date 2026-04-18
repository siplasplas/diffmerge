#include "FileDiffWidget.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QSplitter>

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
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_leftEditor  = new DiffEditor(Side::Left,  splitter);
    m_rightEditor = new DiffEditor(Side::Right, splitter);
    splitter->addWidget(m_leftEditor);
    splitter->addWidget(m_rightEditor);
    splitter->setSizes({1, 1});
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter);

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

    // Character-level highlights for Replace hunks
    const auto charDiff = IntraLineDiffEngine::compute(result, leftLines, rightLines);
    m_leftEditor->setIntraLineDiffs(charDiff.leftRanges);
    m_rightEditor->setIntraLineDiffs(charDiff.rightRanges);
}

void FileDiffWidget::setSyncThreshold(double fraction) {
    m_syncMapper.setThreshold(fraction);
}

double FileDiffWidget::syncThreshold() const {
    return m_syncMapper.threshold();
}

}  // namespace diffmerge::gui
