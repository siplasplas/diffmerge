// FileDiffWidget contains two DiffEditors side-by-side, sharing an
// AlignedLineModel. This stage does NOT include sync scrolling - that
// comes in stage 3. Each editor scrolls independently for now.

#ifndef DIFFMERGE_GUI_FILEDIFFWIDGET_H
#define DIFFMERGE_GUI_FILEDIFFWIDGET_H

#include <QStringList>
#include <QWidget>
#include <memory>

#include <diffcore/DiffTypes.h>

#include "AlignedLineModel.h"

namespace diffmerge::gui {

class DiffEditor;

class FileDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileDiffWidget(QWidget* parent = nullptr);
    ~FileDiffWidget() override;

    // Load two files' contents, run the diff engine, and display. The
    // caller has already done file I/O; we just need the line lists.
    // Pass opts if you want to customize normalization (ignore case, etc.).
    void setContent(const QStringList& leftLines,
                    const QStringList& rightLines,
                    const diffcore::DiffOptions& opts = {});

    // Expose access to the underlying editors for callers that want to
    // wire extra behavior (e.g. sync scroll in stage 3).
    DiffEditor* leftEditor() const { return m_leftEditor; }
    DiffEditor* rightEditor() const { return m_rightEditor; }

private:
    // Build layout (two editors + splitter).
    void setupUi();

    DiffEditor* m_leftEditor;
    DiffEditor* m_rightEditor;
    std::unique_ptr<AlignedLineModel> m_model;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_FILEDIFFWIDGET_H
