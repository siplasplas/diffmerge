#include <QTest>
#include <QSignalSpy>
#include <QUndoStack>
#include <diffmerge/ConflictResolverWidget.h>
#include <diffmerge/MergeWidget.h>
#include <diffmerge/DiffEditor.h>
#include <qce/CodeEditArea.h>
using namespace diffmerge::gui;
class TestResolverWidget : public QObject {
    Q_OBJECT
private slots:
    void automaticUndoAndOverride() {
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        const QByteArray input="head\r\n<<<<<<<\ncheck();\nold();\n|||||||\nold();\n=======\nnew();\n>>>>>>>\ntail";
        QVERIFY(widget.setInput(input,"sample.cpp")); QTRY_COMPARE(finished.size(),1); QVERIFY(finished[0][0].toBool());
        QCOMPARE(widget.resultBytes().value(),QByteArray("head\r\ncheck();\nnew();\ntail")); QCOMPARE(widget.pendingDecisionCount(),0);
        widget.undoStack()->undo(); QCOMPARE(widget.resultBytes().value(),input); QCOMPARE(widget.pendingDecisionCount(),1);
        widget.undoStack()->redo(); QCOMPARE(widget.pendingDecisionCount(),0);
        QVERIFY(widget.applyCandidate(0,"right")); QCOMPARE(widget.resultBytes().value(),QByteArray("head\r\nnew();\ntail"));
        widget.undoStack()->undo(); QCOMPARE(widget.resultBytes().value(),QByteArray("head\r\ncheck();\nnew();\ntail"));
        QVERIFY(!widget.setInput("replacement"));
    }
    void deferredAndNativeEditing() {
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        const QByteArray input="<<<<<<<\nleft();\n=======\nright();\n>>>>>>>\n";
        QVERIFY(widget.setInput(input)); QTRY_COMPARE(finished.size(),1);
        QCOMPARE(widget.pendingDecisionCount(),1); QVERIFY(widget.applyCandidate(0,"right",true));
        QCOMPARE(widget.resultBytes().value(),QByteArray("right();\n")); QCOMPARE(widget.pendingDecisionCount(),1);
        QCOMPARE(widget.plan().decisions[0].state,diffcore::DecisionState::Deferred);
        QVERIFY(!widget.resultBytes()->contains("TODO"));
        QVERIFY(widget.acceptCurrentText(0)); QCOMPARE(widget.pendingDecisionCount(),0);
        widget.undoStack()->undo(); QCOMPARE(widget.pendingDecisionCount(),1); QCOMPARE(widget.plan().decisions[0].state,diffcore::DecisionState::Deferred);
        widget.undoStack()->redo(); QCOMPARE(widget.pendingDecisionCount(),0);
        auto* area=widget.mergeEditor()->resultEditor()->edit()->area(); area->setCursorPosition({0,3}); QTest::keyClicks(area,"X");
        QCOMPARE(widget.pendingDecisionCount(),1); QVERIFY(widget.acceptCurrentText(0)); QCOMPARE(widget.pendingDecisionCount(),0);
        QCOMPARE(widget.report()["status"].toString(),QString("clean"));
    }
    void pendingNavigationAndEvidence() {
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        const QByteArray input="<<<<<<<\nleft();\n=======\nright();\n>>>>>>>\ncontext\n<<<<<<<\nfirst();\n=======\nsecond();\n>>>>>>>\n";
        QVERIFY(widget.setInput(input)); QTRY_COMPARE(finished.size(),1);
        QCOMPARE(widget.pendingDecisionCount(),2); QCOMPARE(widget.mergeEditor()->currentConflictIndex(),0);
        widget.navigateToNextPending(); QCOMPARE(widget.mergeEditor()->currentConflictIndex(),1);
        widget.navigateToPreviousPending(); QCOMPARE(widget.mergeEditor()->currentConflictIndex(),0);
        QVERIFY(!widget.acceptCurrentText(0));
        QVERIFY(widget.applyCandidate(0,"left")); QCOMPARE(widget.pendingDecisionCount(),1);
        widget.navigateToNextPending(); QCOMPARE(widget.mergeEditor()->currentConflictIndex(),1);
        QCOMPARE(widget.plan().input.bytes,input);
        QCOMPARE(widget.report()["status"].toString(),QString("needs-review"));
        widget.undoStack()->undo(); QCOMPARE(widget.pendingDecisionCount(),2);
    }
    void cancellationAndInvalidText() {
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        QVERIFY(widget.setInput("<<<<<<<\na\n=======\nb\n>>>>>>>\n")); widget.cancelAnalysis();
        QTRY_COMPARE(finished.size(),1); QVERIFY(!finished[0][0].toBool());
        finished.clear(); QVERIFY(widget.setInput("=======\n")); QTRY_COMPARE(finished.size(),1); QVERIFY(!finished[0][0].toBool());
    }
};
QTEST_MAIN(TestResolverWidget)
#include "test_resolver_widget.moc"
