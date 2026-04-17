#include "FileDiffWidget.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QSplitter>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>
#include <qce/ViewportState.h>

#include "../editor/DiffEditor.h"

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
    m_leftEditor = new DiffEditor(Side::Left, splitter);
    m_rightEditor = new DiffEditor(Side::Right, splitter);
    splitter->addWidget(m_leftEditor);
    splitter->addWidget(m_rightEditor);
    splitter->setSizes({1, 1});
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter);

    // Synchronized scrolling via qce::CodeEditArea::viewportChanged
    qce::CodeEdit* leftEdit  = m_leftEditor->edit();
    qce::CodeEdit* rightEdit = m_rightEditor->edit();
    connect(leftEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [rightEdit](const qce::ViewportState& vp) {
        rightEdit->area()->verticalScrollBar()->setValue(vp.firstVisibleRow);
    });
    connect(rightEdit->area(), &qce::CodeEditArea::viewportChanged,
            this, [leftEdit](const qce::ViewportState& vp) {
        leftEdit->area()->verticalScrollBar()->setValue(vp.firstVisibleRow);
    });
}

void FileDiffWidget::setContent(const QStringList& leftLines,
                                const QStringList& rightLines,
                                const diffcore::DiffOptions& opts) {
    diffcore::DiffEngine engine;
    const diffcore::DiffResult result = engine.compute(leftLines, rightLines, opts);
    m_model->build(result, leftLines, rightLines);
    m_leftEditor->setAlignedModel(m_model.get());
    m_rightEditor->setAlignedModel(m_model.get());
}

}  // namespace diffmerge::gui
