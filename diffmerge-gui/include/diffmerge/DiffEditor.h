// DiffEditor wraps qce::CodeEdit for side-by-side diff display:
//   - Scrollbar on outer edge (Left pane: left; Right pane: right)
//   - LineNumberGutter on inner edge (Left pane: right rail; Right pane: left rail)
//   - Per-line background colors via setLineBackgroundProvider
//   - Kate syntax colors composed with character-level diff backgrounds

#ifndef DIFFMERGE_GUI_DIFFEDITOR_H
#define DIFFMERGE_GUI_DIFFEDITOR_H

#include <QVector>
#include <QWidget>
#include <memory>
#include <optional>

#include <qce/CodeEdit.h>
#include <qce/IHighlighter.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>

#include <diffmerge/ViewProjection.h>
#include <diffmerge/ColorScheme.h>
#include <diffmerge/AlignedLineModel.h>
#include <diffmerge/IntraLineDiffEngine.h>

namespace diffmerge::gui {

class DiffHighlighter;
class ChangeBoundaryOverlay;
class DiffLineNumberGutter;

class DiffEditor : public QWidget {
    Q_OBJECT
public:
    explicit DiffEditor(Side side, QWidget* parent = nullptr);
    ~DiffEditor() override;

    void setAlignedModel(const AlignedLineModel* model, bool preserveDocument = false);
    void setIntraLineDiffs(const QVector<QVector<IntraLineDiffEngine::CharRange>>& ranges);
    void setColorScheme(const ColorScheme& scheme);
    // GUI-thread only. The file name selects installed Kate XML rules.
    void setSyntaxFileName(const QString& fileName);
    QString syntaxFileName() const { return m_syntaxFileName; }
    QString syntaxLanguage() const { return m_syntaxLanguage; }
    void reloadSyntaxDefinitions();
    // Transient overlays, separate from comparison colors.
    void setRevealOverlay(std::optional<diffcore::LineRange> range,
                          const QVector<int>& searchBoundaries = {});

    void setProjection(std::shared_ptr<const PreparedComparison> comparison,
                       const QVector<ViewRow>& rows, bool unified, const QString& leftSyntaxFileName = {});
    bool hasProjection() const { return bool(m_projectedComparison); }
    int displayLine(int originalLine) const;
    int originalLine(int displayLine) const;
    const QVector<ViewRow>& displayRows() const { return m_rows; }

    void setSide(Side side);
    Side side() const { return m_side; }
    qce::CodeEdit* edit() const { return m_edit; }
    const ColorScheme& colorScheme() const { return m_scheme; }

signals:
    void colorSchemeChanged();
    void foldClicked(int leftLine);

protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyModel(bool preserveDocument = false);
    void applyHighlighter();
    void applyColorScheme(const ColorScheme& scheme);

    void applyProjection();
    std::shared_ptr<const PreparedComparison> m_projectedComparison;
    QVector<ViewRow> m_rows;
    QVector<int> m_originalToDisplay;
    bool m_unified = false;
    std::unique_ptr<qce::IHighlighter> m_projectedHighlighter;
    QString m_leftSyntaxFileName;
    QString m_syntaxFileName;
    QString m_syntaxLanguage;
    Side m_side;
    ColorScheme m_scheme;
    bool m_followSystemPalette = true;
    const AlignedLineModel* m_model = nullptr;

    qce::SimpleTextDocument* m_doc  = nullptr;
    qce::CodeEdit*           m_edit = nullptr;
    ChangeBoundaryOverlay* m_boundaries = nullptr;
    std::unique_ptr<DiffLineNumberGutter> m_lineNumbers;
    std::unique_ptr<DiffHighlighter>       m_highlighter;

    QVector<diffcore::ChangeType>                        m_docLineChanges;
    QVector<QVector<IntraLineDiffEngine::CharRange>>     m_intraLineDiffs;
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_DIFFEDITOR_H
