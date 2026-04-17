// DiffEditor wraps qce::CodeEdit for side-by-side diff display:
//   - Scrollbar on outer edge (Left pane: left; Right pane: right)
//   - LineNumberGutter on inner edge (Left pane: right rail; Right pane: left rail)
//   - Per-line background colors via setLineBackgroundProvider
//   - No filler lines; scroll alignment is handled by ScrollSyncMapper

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

namespace diffmerge::gui {

class DiffEditor : public QWidget {
    Q_OBJECT
public:
    explicit DiffEditor(Side side, QWidget* parent = nullptr);

    void setAlignedModel(const AlignedLineModel* model);
    void setColorScheme(const ColorScheme& scheme);

    Side side() const { return m_side; }
    qce::CodeEdit* edit() const { return m_edit; }

private:
    void applyModel();

    Side m_side;
    ColorScheme m_scheme;
    const AlignedLineModel* m_model = nullptr;

    qce::SimpleTextDocument* m_doc = nullptr;
    qce::CodeEdit* m_edit = nullptr;
    std::unique_ptr<qce::LineNumberGutter> m_lineNumbers;

    // Per-doc-line change types; read by the line background provider lambda.
    QVector<diffcore::ChangeType> m_docLineChanges;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFEDITOR_H
