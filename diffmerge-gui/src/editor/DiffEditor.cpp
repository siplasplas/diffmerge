#include "DiffEditor.h"

#include <QFontDatabase>
#include <QVBoxLayout>

#include <qce/CodeEditArea.h>

namespace diffmerge::gui {

DiffEditor::DiffEditor(Side side, QWidget* parent)
    : QWidget(parent), m_side(side), m_scheme(ColorScheme::forSystem()) {

    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);
    m_edit->area()->setReadOnly(true);
    m_edit->area()->setWordWrap(false);

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

void DiffEditor::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    m_edit->area()->viewport()->update();
}

void DiffEditor::applyModel() {
    if (!m_model) {
        m_doc->setLines({});
        m_docLineChanges.clear();
        return;
    }

    m_doc->setLines(m_model->documentLines(m_side));

    const int docCount = m_doc->lineCount();
    m_docLineChanges.resize(docCount);
    for (int i = 0; i < docCount; ++i) {
        m_docLineChanges[i] = m_model->docLineChangeType(m_side, i);
    }
}

}  // namespace diffmerge::gui
