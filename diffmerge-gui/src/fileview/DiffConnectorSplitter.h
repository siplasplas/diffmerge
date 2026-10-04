#ifndef DIFFMERGE_GUI_DIFFCONNECTORSPLITTER_H
#define DIFFMERGE_GUI_DIFFCONNECTORSPLITTER_H

#include <QSplitter>

namespace diffmerge::gui {

class AlignedLineModel;
class DiffEditor;

// A draggable divider connecting document ranges in the two editor viewports.
// The model must outlive the splitter. The splitter takes ownership of editors.
class DiffConnectorSplitter : public QSplitter {
public:
    DiffConnectorSplitter(DiffEditor* left, DiffEditor* right,
                          const AlignedLineModel* model, QWidget* parent = nullptr);

    void setModel(const AlignedLineModel* model);
    void updateConnections();

protected:
    QSplitterHandle* createHandle() override;

private:
    DiffEditor* m_left;
    DiffEditor* m_right;
    const AlignedLineModel* m_model;
};

}  // namespace diffmerge::gui

#endif
