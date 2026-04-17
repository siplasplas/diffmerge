#ifndef DIFFMERGE_GUI_FILEDIFFWIDGET_H
#define DIFFMERGE_GUI_FILEDIFFWIDGET_H

#include <QStringList>
#include <QWidget>
#include <memory>

#include <diffcore/DiffTypes.h>

#include "AlignedLineModel.h"
#include "ScrollSyncMapper.h"

namespace diffmerge::gui {

class DiffEditor;

class FileDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileDiffWidget(QWidget* parent = nullptr);
    ~FileDiffWidget() override;

    void setContent(const QStringList& leftLines,
                    const QStringList& rightLines,
                    const diffcore::DiffOptions& opts = {});

    // Fraction of viewport height at which the sync line sits (0 < t < 1).
    void setSyncThreshold(double fraction);
    double syncThreshold() const;

    DiffEditor* leftEditor()  const { return m_leftEditor; }
    DiffEditor* rightEditor() const { return m_rightEditor; }

private:
    void setupUi();

    DiffEditor* m_leftEditor  = nullptr;
    DiffEditor* m_rightEditor = nullptr;
    std::unique_ptr<AlignedLineModel> m_model;
    ScrollSyncMapper m_syncMapper;
    bool m_syncingScroll = false;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_FILEDIFFWIDGET_H
