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

#include <diffmerge/AlignedLineModel.h>
#include <diffmerge/ScrollSyncMapper.h>
#include <diffmerge/Comparison.h>
#include <diffmerge/ViewProjection.h>

namespace diffmerge::gui {

class DiffEditor;
class DiffConnectorSplitter;

class FileDiffWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileDiffWidget(QWidget* parent = nullptr);
    ~FileDiffWidget() override;

    void setContent(const QStringList& leftLines,
                    const QStringList& rightLines,
                    const diffcore::DiffOptions& opts = {.alignWhitespaceChanges = true});

    // GUI-thread only. Null clears the view; no algorithms are run here.
    void setComparison(std::shared_ptr<const PreparedComparison> comparison);
    void clearComparison() { setComparison(nullptr); }
    std::shared_ptr<const PreparedComparison> comparison() const { return m_comparison; }
    std::chrono::nanoseconds lastInstallationTime() const { return m_installationTime; }

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }
    void setUnchangedLinesSkipped(bool skipped);
    bool unchangedLinesSkipped() const { return m_skipUnchanged; }
    void setContextLines(int lines);
    int contextLines() const { return m_contextLines; }
    // Width in logical pixels of the connector between the panes (8..160).
    void setPanelSpacing(int pixels);
    int panelSpacing() const;
    DiffEditor* unifiedEditor() const { return m_unifiedEditor; }

    // Original coordinates, never indices from another diff implementation.
    // Invalid inputs return false and leave the view unchanged. count == 0
    // reveals a boundary, including lineCount in an empty or nonempty file.
    bool revealLines(Side side, diffcore::LineRange range, bool emphasize = false);
    bool revealText(Side side, TextRange range, bool emphasize = false);
    bool setSearchHighlights(Side side, const QVector<TextRange>& ranges);
    void clearSearchHighlights();
    // For in-memory content without snapshot file names. No file is opened.
    void setSyntaxFileName(Side side, const QString& fileName);
    void reloadSyntaxDefinitions();
    bool navigateToChange(int index);
    int changeCount() const;
    int currentChangeIndex() const { return m_currentHunk; } // -1 = no selection.
    const QVector<ChangeBlock>& changes() const;

    // Hide application controls when the host supplies its own toolbar.
    void setPathBarVisible(bool visible);
    void setNavigationBarVisible(bool visible);

    // Load files from disk, update path bar, and show diff.
    // Returns false and emits loadFailed if either file cannot be read.
    bool loadFromPaths(const QString& leftPath, const QString& rightPath);

    // Fill path edits without loading (used for command-line pre-fill).
    void setPaths(const QString& leftPath, const QString& rightPath);

    // Apply a host-selected path and reload when both paths are present.
    void setPath(Side side, const QString& path);

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
    void viewModeChanged(ViewMode mode);
    void unchangedLinesSkippedChanged(bool skipped);
    void backRequested();
    void fileBrowseRequested(Side side, const QString& currentPath);
    void loadFailed(const QString& message);
    void currentChangeChanged(int index);
    void comparisonChanged(int changeCount);
    // Emitted after a successful loadFromPaths so MainWindow can update title.
    void pathsChanged(const QString& leftPath, const QString& rightPath);

private:
    void updateHorizontalScrollRange();
    bool m_syncingHorizontal = false;
    int m_horizontalOffset = 0;
    int m_leftColumns = 0, m_rightColumns = 0;
    void rebuildProjection();
    void openContaining(Side side, diffcore::LineRange range);
    int unifiedLine(Side side, int line) const;
    ViewMode m_viewMode = ViewMode::SideBySide;
    bool m_skipUnchanged = false;
    int m_contextLines = 3;
    QSet<int> m_openedFolds;
    ViewProjection m_projection;
    DiffEditor* m_unifiedEditor = nullptr;
    void setupUi();
    void navigateToHunk(int idx);
    void updateNavLabel();
    void refreshSearchHighlights();
    bool validTextRange(Side side, TextRange range) const;
    void onBrowseLeft();
    void onBrowseRight();
    void reloadFromPathBar();

    QVector<TextRange> m_leftSearch, m_rightSearch;
    std::optional<std::pair<Side, diffcore::LineRange>> m_emphasis;
    QWidget* m_pathBar = nullptr;
    QWidget* m_navigationBar = nullptr;
    DiffEditor*   m_leftEditor  = nullptr;
    DiffEditor*   m_rightEditor = nullptr;
    DiffConnectorSplitter* m_splitter = nullptr;
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
    std::shared_ptr<const PreparedComparison> m_comparison;
    const AlignedLineModel* m_model = nullptr;
    int m_notifiedChange = -1;
    std::chrono::nanoseconds m_installationTime{};
    ScrollSyncMapper m_syncMapper;
    bool m_syncingScroll = false;
    bool m_navigating = false;
    Side m_navigationSide = Side::Left;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_FILEDIFFWIDGET_H
