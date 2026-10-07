#include <QTest>
#include <QSignalSpy>
#include <QUndoStack>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
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
    void focusedMovedGroupAndHostLabels() {
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        const QByteArray base="context();\nint oldIndex = append(oldItem);\nint removed = limit();\nint finalIndex = oldIndex - removed;\n";
        const QByteArray left="context();\nint finalIndex = append(oldItem);\n";
        const QByteArray right="moveItem(oldItem);\n}\nvoid helper(Item* item) {\nint oldIndex = append(item);\nint removed = limit();\nint finalIndex = oldIndex - removed;\ncontinuation();\n";
        const QByteArray input="<<<<<<< original-left\n"+left+"||||||| ancestor\n"+base+"=======\n"+right+">>>>>>> original-right\n";
        QVERIFY(widget.setInput(input)); widget.setSourceLabels({"main","base revision","codeedit"});
        QTRY_COMPARE(finished.size(),1); QVERIFY(finished[0][0].toBool()); QCOMPARE(widget.pendingDecisionCount(),1);
        QVERIFY(widget.mergeEditor()->isHidden());
        auto* raw=widget.findChild<QPlainTextEdit*>("resolverRawSource"); QVERIFY(raw); QVERIFY(raw->isHidden());
        auto* context=widget.findChild<QPlainTextEdit*>("resolverSharedPrefix"); QVERIFY(context);
        QVERIFY(context->toPlainText().contains("moveItem(oldItem)")); QVERIFY(!context->toPlainText().contains("<<<<<<<"));
        QCOMPARE(context->extraSelections().size(),1);
        const auto adapted=widget.findChild<QPlainTextEdit*>("resolverVariant_adapted"); QVERIFY(adapted);
        QCOMPARE(adapted->toPlainText(),QString("int finalIndex = append(item);\n"));
        const auto replayed=widget.findChild<QPlainTextEdit*>("resolverVariant_replayed"); QVERIFY(replayed);
        QCOMPARE(replayed->toPlainText(),QString("int finalIndex = append(oldItem);\n"));
        const auto original=widget.findChild<QPlainTextEdit*>("resolverVariant_right"); QVERIFY(original);
        QCOMPARE(original->toPlainText(),QString("int oldIndex = append(item);\nint removed = limit();\nint finalIndex = oldIndex - removed;\n"));
        bool sawMain=false,sawTarget=false;
        for(auto* label:widget.findChildren<QLabel*>()) { sawMain|=label->text().contains("main"); sawTarget|=label->text().contains("codeedit"); }
        QVERIFY(sawMain); QVERIFY(sawTarget);
        auto* choose=widget.findChild<QPushButton*>("resolverChoose_adapted"); QVERIFY(choose); choose->click();
        QCOMPARE(widget.pendingDecisionCount(),0); QVERIFY(widget.resultBytes()->contains("int finalIndex = append(item);"));
        QVERIFY(widget.resultBytes()->endsWith("continuation();\n"));
        widget.undoStack()->undo(); QCOMPARE(widget.resultBytes().value(),input); QCOMPARE(widget.pendingDecisionCount(),1);
        QCOMPARE(widget.report()["sourceLabels"].toObject()["right"].toString(),QString("codeedit"));
        for(auto* check:widget.findChildren<QCheckBox*>()) if(check->text()=="Show conflict source and edit RESULT") check->setChecked(true);
        QVERIFY(!raw->isHidden()); QCOMPARE(raw->toPlainText(),QString::fromUtf8(input)); QVERIFY(!widget.mergeEditor()->isHidden());
    }
    void hostSessionAndDraftIdentity() {
        MergeSessionInputs inputs; MergeResultSeed seed;
        seed.file.availability=MergeAvailability::Present;
        seed.file.bytes="<<<<<<<\nleft();\n=======\nright();\n>>>>>>>\n";
        seed.file.rawPath="sample.cpp"; seed.file.mode=0100644; seed.fingerprint="host-conflict-identity";
        inputs.resultSeed=seed; const auto prepared=prepareMergeSession(inputs); QVERIFY(prepared.session);
        ConflictResolverWidget widget; QSignalSpy finished(&widget,&ConflictResolverWidget::analysisFinished);
        QVERIFY(widget.setSession(prepared.session)); QTRY_COMPARE(finished.size(),1);
        QCOMPARE(widget.mergeEditor()->session(),prepared.session);
        QVERIFY(widget.applyCandidate(0,"right",true));
        const auto draft=widget.captureDraft(); QVERIFY(draft); QCOMPARE(draft->merge.session,prepared.session);
        ConflictResolverWidget restored; QVERIFY(restored.restoreDraft(*draft));
        QCOMPARE(restored.pendingDecisionCount(),1); QCOMPARE(restored.plan().decisions[0].state,diffcore::DecisionState::Deferred);
        QCOMPARE(restored.resultBytes().value(),QByteArray("right();\n"));
        QVERIFY(restored.acceptCurrentText(0)); const auto capture=restored.mergeEditor()->captureExportInput(); QVERIFY(capture);
        QCOMPARE(capture->session,prepared.session);
        QCOMPARE(capture->session->inputs().resultSeed->fingerprint,seed.fingerprint);
        QCOMPARE(capture->session->inputs().resultSeed->file.rawPath,seed.file.rawPath);
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
