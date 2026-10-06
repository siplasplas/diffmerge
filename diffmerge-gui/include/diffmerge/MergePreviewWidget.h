#pragma once
#include <QWidget>
#include <diffmerge/ConflictMarkers.h>
class QLabel;
class QSplitter;
class QToolButton;
namespace diffmerge::gui {
class DiffEditor;
// Read-only inspection. No key bindings, save actions or implicit resolution.
class MergePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit MergePreviewWidget(QWidget* parent = nullptr);
    virtual bool setSession(std::shared_ptr<const PreparedMergeSession> session,
                    const MarkerImportOptions& options = {});
    std::shared_ptr<const PreparedMergeSession> session() const { return m_session; }
    virtual const QVector<ImportedConflict>& markerConflicts() const { return m_conflicts; }
    int currentConflictIndex() const { return m_current; }
    virtual bool navigateToConflict(int index);
    void navigateToNextConflict();
    void navigateToPreviousConflict();
    void setPanelSpacing(int pixels); // 8..160; default 24 logical pixels.
    int panelSpacing() const;
    void setBaseVisible(bool visible);
    bool baseVisible() const;
    DiffEditor* sourceEditor(MergeSource source) const;
    DiffEditor* resultEditor() const { return m_result; }
signals:
    void currentConflictChanged(int index);
    void operationFailed(const QString& message);
protected:
    void updateSources();
    virtual void updateSummary();
    std::shared_ptr<const PreparedMergeSession> m_session;
    QVector<ImportedConflict> m_conflicts;
    int m_current = -1;
    QSplitter* m_splitter;
    DiffEditor *m_ours, *m_result, *m_theirs, *m_base;
    QLabel *m_oursLabel, *m_resultLabel, *m_theirsLabel, *m_baseLabel, *m_summary;
    QWidget* m_basePane;
    QToolButton *m_previous, *m_next, *m_showBase;
};
} // namespace diffmerge::gui
