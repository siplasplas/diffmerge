// DiffEditor wraps qce::CodeEdit for side-by-side diff display:
//   - Scrollbar on outer edge (Left pane: left; Right pane: right)
//   - LineNumberGutter on inner edge (Left pane: right rail; Right pane: left rail)
//   - Per-line background colors via setLineBackgroundProvider
//   - DiffHighlighter for character-level highlighting within Replace lines

#ifndef DIFFMERGE_GUI_DIFFEDITOR_H
#define DIFFMERGE_GUI_DIFFEDITOR_H

#include <QVector>
#include <QWidget>
#include <memory>

#include <qce/CodeEdit.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>

#include "../common/ColorScheme.h"
#include "../fileview/AlignedLineModel.h"
#include "DiffHighlighter.h"
#include "IntraLineDiffEngine.h"

namespace diffmerge::gui {

class DiffEditor : public QWidget {
    Q_OBJECT
public:
    explicit DiffEditor(Side side, QWidget* parent = nullptr);

    void setAlignedModel(const AlignedLineModel* model);
    void setIntraLineDiffs(const QVector<QVector<IntraLineDiffEngine::CharRange>>& ranges);
    void setColorScheme(const ColorScheme& scheme);

    Side side() const { return m_side; }
    qce::CodeEdit* edit() const { return m_edit; }

private:
    void applyModel();
    void applyHighlighter();

    Side m_side;
    ColorScheme m_scheme;
    const AlignedLineModel* m_model = nullptr;

    qce::SimpleTextDocument* m_doc  = nullptr;
    qce::CodeEdit*           m_edit = nullptr;
    std::unique_ptr<qce::LineNumberGutter> m_lineNumbers;
    std::unique_ptr<DiffHighlighter>       m_highlighter;

    QVector<diffcore::ChangeType>                        m_docLineChanges;
    QVector<QVector<IntraLineDiffEngine::CharRange>>     m_intraLineDiffs;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFEDITOR_H
