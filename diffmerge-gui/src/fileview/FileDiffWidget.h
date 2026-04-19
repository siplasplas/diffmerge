#ifndef DIFFMERGE_GUI_FILEDIFFWIDGET_H
#define DIFFMERGE_GUI_FILEDIFFWIDGET_H

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QToolButton>
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

    // Load files from disk, update path bar, and show diff.
    // Returns false and shows an error if either file cannot be read.
    bool loadFromPaths(const QString& leftPath, const QString& rightPath);

    // Fraction of viewport height at which the sync line sits (0 < t < 1).
    void setSyncThreshold(double fraction);
    double syncThreshold() const;

    DiffEditor* leftEditor()  const { return m_leftEditor; }
    DiffEditor* rightEditor() const { return m_rightEditor; }

    // Show or hide the "← Back" button leading to the directory view.
    void setBackVisible(bool visible);

public slots:
    void navigateToNext();
    void navigateToPrev();

signals:
    void backRequested();
    // Emitted after a successful loadFromPaths so MainWindow can update title.
    void pathsChanged(const QString& leftPath, const QString& rightPath);

private:
    void setupUi();
    void navigateToHunk(int idx);
    void updateNavLabel();
    void onBrowseLeft();
    void onBrowseRight();
    void reloadFromPathBar();

    DiffEditor*   m_leftEditor  = nullptr;
    DiffEditor*   m_rightEditor = nullptr;
    QToolButton*  m_backButton  = nullptr;
    QFrame*       m_backSep     = nullptr;
    QToolButton*  m_prevButton  = nullptr;
    QToolButton*  m_nextButton  = nullptr;
    QLabel*       m_navLabel    = nullptr;
    int           m_currentHunk = -1;
    QLineEdit*    m_leftPathEdit  = nullptr;
    QToolButton*  m_leftBrowse    = nullptr;
    QLineEdit*    m_rightPathEdit = nullptr;
    QToolButton*  m_rightBrowse   = nullptr;
    std::unique_ptr<AlignedLineModel> m_model;
    ScrollSyncMapper m_syncMapper;
    bool m_syncingScroll = false;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_FILEDIFFWIDGET_H
