#pragma once
#include <diffmerge/MergePreviewWidget.h>
#include <diffmerge/MergeExport.h>
#include <memory>
namespace diffmerge::gui {
struct MergeEditingState;
// Sources remain immutable. RESULT edits and explicit resolution decisions share
// the result editor's native Undo stack. This component performs no host writes.
class MergeWidget : public MergePreviewWidget {
    Q_OBJECT
public:
    explicit MergeWidget(QWidget* parent = nullptr);
    ~MergeWidget() override;
    bool setSession(std::shared_ptr<const PreparedMergeSession> session,
                    const MarkerImportOptions& options = {}) override;
    void setEditable(bool editable);
    bool isEditable() const;
    bool isModified() const;
    QString resultText() const;
    // Exact serialization of current text/endings/BOM. Invalid UTF-16, literal CR
    // in editor lines or an exceeded limit returns nullopt, never lossy bytes.
    std::optional<QByteArray> resultBytes() const;
    QVector<MergeEditableConflict> conflicts() const;
    std::optional<MergeExportInput> captureExportInput() const;
    PrepareMergeExportResult exportResult(const MergeExportOptions& options = {},
        const MergeSessionLimits& limits = {}, const diffcore::CancellationToken& cancellation = {}) const;
    // Emit owned requests only; the host validates, exports and performs I/O.
    bool requestSave(const MergeExportOptions& options = {});
    bool requestExport(const MergeExportOptions& options = {});
    bool requestFinish(const MergeExportOptions& options);
    // Host acknowledgement after a successful unchanged save; retains native Undo.
    bool acknowledgeSaved(const MergeExportInput& captured);
    int unresolvedCount() const;
    const QVector<ImportedConflict>& markerConflicts() const override;
    void navigateToNextUnresolvedConflict();
    void navigateToPreviousUnresolvedConflict();
    bool canChooseConflict(int index, MergeChoice choice) const;
    bool chooseConflict(int index, MergeChoice choice);
    bool markConflictResolved(int index);
    bool markConflictUnresolved(int index);
    // Explicit host/user review for a range that became ambiguous after editing.
    bool reviewConflictRange(int index, MergeResultRange range);
    void discardChanges(); // Explicit restoration to the original seed and states.
    bool navigateToConflict(int index) override;
signals:
    void editableChanged(bool editable);
    void modifiedChanged(bool modified);
    void unresolvedCountChanged(int unresolved);
    void conflictStatesChanged();
    void saveRequested(diffmerge::gui::MergeExportInput captured, diffmerge::gui::MergeExportOptions options);
    void exportRequested(diffmerge::gui::MergeExportInput captured, diffmerge::gui::MergeExportOptions options);
    void finishRequested(diffmerge::gui::MergeExportInput captured, diffmerge::gui::MergeExportOptions options);
protected:
    void updateSummary() override;
private:
    void documentEdited();
    void undoIndexChanged(int index);
    void notifyState();
    bool changeState(int index, MergeResolutionState state, MergeChoice choice);
    std::unique_ptr<MergeEditingState> m_editing;
    QWidget* m_actions = nullptr;
    QVector<QToolButton*> m_choices;
    QToolButton *m_markResolved = nullptr, *m_markUnresolved = nullptr;
};
} // namespace diffmerge::gui
