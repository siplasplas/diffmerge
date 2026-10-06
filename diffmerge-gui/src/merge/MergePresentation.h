#pragma once
#include <QObject>
#include <memory>
class QSplitter;
class QWidget;
namespace diffmerge::gui {
class MergePreviewWidget;
struct MergePresentationState;
QSplitter* createMergeSplitter(QWidget* parent);
// Observational comparisons only: no editor document resets or resolution decisions.
class MergePresentation : public QObject {
public:
    MergePresentation(MergePreviewWidget* owner, QSplitter* splitter);
    ~MergePresentation() override;
    void requestUpdate();
    bool isUpdating() const;
private:
    void startUpdate();
    void decorateConflicts();
    void synchronizeVertical(int pane);
    void updateHorizontalRanges();
    std::unique_ptr<MergePresentationState> m_state;
};
}
