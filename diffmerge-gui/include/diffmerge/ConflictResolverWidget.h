#pragma once
#include <QWidget>
#include <diffcore/ConflictResolution.h>
#include <memory>
class QUndoStack;
namespace diffmerge::gui {
class MergeWidget;
struct ConflictResolverState;
// Generic review UI. The host supplies bytes and owns all saving/repository actions.
class ConflictResolverWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConflictResolverWidget(QWidget* parent = nullptr);
    ~ConflictResolverWidget() override;
    bool setInput(const QByteArray& bytes, const QString& fileName = {},
        const diffcore::MarkerOptions& markers = {}, const diffcore::ResolutionOptions& options = {});
    // Display metadata supplied by a host, independent of marker labels and target selection.
    void setSourceLabels(const diffcore::ResolutionSourceLabels& labels);
    void cancelAnalysis();
    bool isAnalyzing() const;
    bool isModified() const;
    int pendingDecisionCount() const;
    std::optional<QByteArray> resultBytes() const;
    QJsonObject report() const;
    const diffcore::ResolutionPlan& plan() const;
    MergeWidget* mergeEditor() const;
    QUndoStack* undoStack() const;
    bool applyCandidate(int index, const QString& candidateId, bool deferred = false);
    bool acceptCurrentText(int index);
    void navigateToNextPending();
    void navigateToPreviousPending();
signals:
    void analysisFinished(bool success);
    void stateChanged();
    void operationFailed(const QString& message);
private:
    void refresh();
    void selectDecision(int index);
    void updateReviewPresentation(int index);
    std::unique_ptr<ConflictResolverState> m_state;
};
} // namespace diffmerge::gui
