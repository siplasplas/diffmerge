#include <QTest>
#include <QSignalSpy>
#include <QUndoStack>
#include <diffmerge/MergeWidget.h>
#include <diffmerge/DiffEditor.h>
#include <qce/CodeEditArea.h>
using namespace diffmerge::gui;
class TestMergeEditing : public QObject {
    Q_OBJECT
    static std::shared_ptr<const PreparedMergeSession> session(const QByteArray& bytes) {
        MergeSessionInputs inputs; MergeResultSeed seed; seed.file.availability=MergeAvailability::Present; seed.file.bytes=bytes;
        inputs.resultSeed=seed; return prepareMergeSession(inputs).session;
    }
    static QByteArray seed() {
        return "before\r\n<<<<<<<\r\nours\r\n|||||||\r\nbase\r\n=======\r\ntheirs\r\n>>>>>>>\r\nbetween\n<<<<<<<\nours2\n=======\ntheirs2\n>>>>>>>\nafter";
    }
    static void load(MergeWidget& widget,const QByteArray& bytes) {
        QVERIFY(widget.setSession(session(bytes),{.allowUnconfirmedMarkers=true})); widget.setEditable(true);
    }
private slots:
    void threePanesShowImportedFragmentsWhenEditing() {
        MergeWidget widget; widget.resize(1200,700); widget.show();
        load(widget,seed()); QTest::qWait(10);
        auto* ours=widget.sourceEditor(MergeSource::Ours);
        auto* theirs=widget.sourceEditor(MergeSource::Theirs);
        QVERIFY(ours->isVisible()); QVERIFY(theirs->isVisible()); QVERIFY(widget.resultEditor()->isVisible());
        QVERIFY(ours->width()>100); QVERIFY(theirs->width()>100); QVERIFY(widget.resultEditor()->width()>100);
        QCOMPARE(ours->edit()->area()->document()->lineAt(0),QString("ours"));
        QCOMPARE(theirs->edit()->area()->document()->lineAt(0),QString("theirs"));
        QCOMPARE(widget.resultEditor()->edit()->area()->cursorPosition().line,1);
        QVERIFY(widget.chooseConflict(0,MergeChoice::Ours));
        QCOMPARE(theirs->edit()->area()->document()->lineAt(0),QString("theirs"));
    }
    void fragmentChoices_data() {
        QTest::addColumn<MergeChoice>("choice"); QTest::addColumn<QByteArray>("fragment");
        QTest::newRow("ours") << MergeChoice::Ours << QByteArray("ours\r\n");
        QTest::newRow("theirs") << MergeChoice::Theirs << QByteArray("theirs\r\n");
        QTest::newRow("ours-theirs") << MergeChoice::OursThenTheirs << QByteArray("ours\r\ntheirs\r\n");
        QTest::newRow("theirs-ours") << MergeChoice::TheirsThenOurs << QByteArray("theirs\r\nours\r\n");
        QTest::newRow("base") << MergeChoice::Base << QByteArray("base\r\n");
        QTest::newRow("delete") << MergeChoice::Delete << QByteArray{};
    }
    void fragmentChoices() {
        QFETCH(MergeChoice,choice); QFETCH(QByteArray,fragment);
        MergeWidget widget; load(widget,seed()); auto* area=widget.resultEditor()->edit()->area();
        auto* document=area->document(); const int count=area->undoStack()->count();
        QVERIFY(widget.chooseConflict(0,choice)); QCOMPARE(area->undoStack()->count(),count+1); QCOMPARE(area->document(),document);
        QCOMPARE(widget.unresolvedCount(),1); QCOMPARE(widget.conflicts()[0].choice,choice); QVERIFY(widget.isModified());
        QVERIFY(widget.resultBytes()); QVERIFY(widget.resultBytes()->startsWith("before\r\n"+fragment+"between\n"));
        QVERIFY(widget.resultBytes()->endsWith("after"));
        area->undo(); QCOMPARE(widget.resultBytes().value(),seed()); QCOMPARE(widget.unresolvedCount(),2); QVERIFY(!widget.isModified());
        area->redo(); QCOMPARE(widget.unresolvedCount(),1); QVERIFY(widget.resultBytes()->startsWith("before\r\n"+fragment+"between\n"));
    }
    void alternatingChoicesAndNativeUndoRedo() {
        MergeWidget widget; load(widget,seed()); auto* area=widget.resultEditor()->edit()->area();
        QVERIFY(widget.chooseConflict(0,MergeChoice::Ours)); widget.navigateToNextUnresolvedConflict(); QCOMPARE(widget.currentConflictIndex(),1);
        widget.navigateToPreviousUnresolvedConflict(); QCOMPARE(widget.currentConflictIndex(),1);
        QVERIFY(widget.chooseConflict(1,MergeChoice::Theirs));
        const auto accepted=widget.resultBytes().value(); QCOMPARE(widget.unresolvedCount(),0);
        QCOMPARE(accepted,QByteArray("before\r\nours\r\nbetween\ntheirs2\nafter"));
        area->setCursorPosition({0,3}); QTest::keyClicks(area,"xyz");
        QVERIFY(widget.resultBytes()->startsWith("befxyzore\r\n")); QCOMPARE(widget.unresolvedCount(),0);
        area->undo(); QCOMPARE(widget.resultBytes().value(),accepted); QCOMPARE(widget.unresolvedCount(),0);
        area->undo(); QCOMPARE(widget.unresolvedCount(),1); area->undo(); QCOMPARE(widget.resultBytes().value(),seed()); QVERIFY(!widget.isModified());
        area->redo(); area->redo(); QCOMPARE(widget.resultBytes().value(),accepted); QCOMPARE(widget.unresolvedCount(),0);
        area->redo(); QVERIFY(widget.resultBytes()->startsWith("befxyzore\r\n")); QCOMPARE(widget.unresolvedCount(),0);
    }
    void manualEditsRequireExplicitResolutionAndPreserveUndo() {
        MergeWidget widget; load(widget,seed()); auto* area=widget.resultEditor()->edit()->area();
        area->setCursorPosition({2,2}); QTest::keyClicks(area,"XYZ");
        QCOMPARE(widget.conflicts()[0].state,MergeResolutionState::NeedsReview); QCOMPARE(widget.unresolvedCount(),2);
        QVERIFY(widget.markConflictResolved(0)); QCOMPARE(widget.unresolvedCount(),1);
        area->undo(); QCOMPARE(widget.conflicts()[0].state,MergeResolutionState::NeedsReview);
        area->undo(); QCOMPARE(widget.resultBytes().value(),seed()); QCOMPARE(widget.conflicts()[0].state,MergeResolutionState::Unresolved);
        area->redo(); QCOMPARE(widget.conflicts()[0].state,MergeResolutionState::NeedsReview);
        area->redo(); QCOMPARE(widget.unresolvedCount(),1);
        QVERIFY(widget.markConflictUnresolved(0)); QCOMPARE(widget.unresolvedCount(),2);
        area->undo(); QCOMPARE(widget.unresolvedCount(),1);
    }
    void newlineInsertionAndAmbiguousRangeReview() {
        MergeWidget widget; load(widget,seed()); auto* area=widget.resultEditor()->edit()->area();
        const auto second=widget.conflicts()[1].range.start;
        area->setCursorPosition({0,3}); QTest::keyClick(area,Qt::Key_Return);
        QCOMPARE(widget.conflicts()[1].range.start,second+1); QVERIFY(widget.resultBytes()->startsWith("bef\r\nore\r\n"));
        area->undo(); QCOMPARE(widget.resultBytes().value(),seed());
        area->setSelection({1,0},{8,0}); QTest::keyClick(area,Qt::Key_Backspace);
        QVERIFY(!widget.conflicts()[0].mapped); QVERIFY(!widget.chooseConflict(0,MergeChoice::Ours));
        QVERIFY(widget.reviewConflictRange(0,{7,0})); QVERIFY(widget.markConflictResolved(0));
        area->undo(); area->undo(); QVERIFY(!widget.conflicts()[0].mapped);
        area->undo(); QCOMPARE(widget.resultBytes().value(),seed()); QVERIFY(widget.conflicts()[0].mapped);
    }
    void hostInputsMissingSidesAndSessionReplacement() {
        MergeSessionInputs inputs; MergeResultSeed result; result.file.availability=MergeAvailability::Present; result.file.bytes="manual\n"; inputs.resultSeed=result;
        inputs.ours.availability=MergeAvailability::Absent; inputs.theirs.availability=MergeAvailability::Present; inputs.theirs.bytes="replacement\n";
        inputs.hostConflicts=QVector<MergeConflict>{{.id="host",.theirs=diffcore::LineRange{0,1},.result=diffcore::LineRange{0,1}}};
        const auto prepared=prepareMergeSession(inputs).session; QVERIFY(prepared);
        MergeWidget widget; QVERIFY(widget.setSession(prepared)); widget.setEditable(true);
        QVERIFY(!widget.canChooseConflict(0,MergeChoice::Ours)); QVERIFY(!widget.chooseConflict(0,MergeChoice::Ours));
        QVERIFY(!widget.chooseConflict(0,MergeChoice::Base)); QVERIFY(widget.chooseConflict(0,MergeChoice::Theirs));
        QCOMPARE(widget.resultBytes().value(),QByteArray("replacement\n"));
        QVERIFY(!widget.setSession(session("new"))); widget.discardChanges(); QCOMPARE(widget.resultBytes().value(),QByteArray("manual\n"));
        QVERIFY(!widget.isModified()); QCOMPARE(widget.unresolvedCount(),1);
        QVERIFY(widget.setSession(session("new"))); QCOMPARE(widget.resultBytes().value(),QByteArray("new"));
    }
    void hostMarkerCoalescingUsesImmutableSources() {
        MergeSessionInputs inputs; MergeResultSeed seed; seed.file.availability=MergeAvailability::Present;
        seed.file.bytes="<<<<<<<\nmarker ours\n=======\nmarker theirs\n>>>>>>>\n"; inputs.resultSeed=seed;
        inputs.ours.availability=inputs.theirs.availability=MergeAvailability::Present;
        inputs.ours.bytes="stage ours\n"; inputs.theirs.bytes="stage theirs\n";
        inputs.hostConflicts=QVector<MergeConflict>{{.id="host-one",.ours=diffcore::LineRange{0,1},.theirs=diffcore::LineRange{0,1},.result=diffcore::LineRange{0,5}}};
        const auto prepared=prepareMergeSession(inputs).session; QVERIFY(prepared);
        MergeWidget widget; QVERIFY(widget.setSession(prepared)); widget.setEditable(true);
        QCOMPARE(widget.conflicts().size(),1); QCOMPARE(widget.conflicts()[0].id,QString("host-one"));
        QVERIFY(widget.chooseConflict(0,MergeChoice::Ours)); QCOMPARE(widget.resultBytes().value(),QByteArray("stage ours\n"));
        widget.resultEditor()->edit()->area()->undo(); QCOMPARE(widget.resultBytes().value(),seed.file.bytes);
    }
    void undoBranchAndUnicodeRemainExact() {
        MergeWidget widget; load(widget,QByteArray::fromHex("efbbbf")+seed()); auto* area=widget.resultEditor()->edit()->area();
        QVERIFY(widget.chooseConflict(0,MergeChoice::Theirs)); area->undo();
        QVERIFY(widget.chooseConflict(0,MergeChoice::Base)); QCOMPARE(widget.conflicts()[0].choice,MergeChoice::Base);
        QVERIFY(!area->undoStack()->canRedo());
        area->setCursorPosition({0,2}); QTest::keyClicks(area,"xy");
        const auto edited=widget.resultBytes().value(); QVERIFY(edited.startsWith(QByteArray::fromHex("efbbbf")+"bexyfore\r\n"));
        area->undo(); area->undo(); QCOMPARE(widget.resultBytes().value(),QByteArray::fromHex("efbbbf")+seed());
        area->redo(); area->redo(); QCOMPARE(widget.resultBytes().value(),edited);
        MergeWidget unicode; load(unicode,QByteArray::fromHex("efbbbf")+"x\n<<<<<<<\n"+QByteArray::fromHex("f09f9880")+"\n=======\ny\n>>>>>>>\ntail");
        QVERIFY(unicode.chooseConflict(0,MergeChoice::Ours));
        QCOMPARE(unicode.resultBytes().value(),QByteArray::fromHex("efbbbf")+"x\n"+QByteArray::fromHex("f09f9880")+"\ntail");
    }
    void bomEmptyFileAndReadonly() {
        MergeWidget widget; load(widget,QByteArray::fromHex("efbbbf")); QVERIFY(widget.resultBytes());
        auto* area=widget.resultEditor()->edit()->area(); QTest::keyClicks(area,"text");
        QCOMPARE(widget.resultBytes().value(),QByteArray::fromHex("efbbbf")+"text"); area->undo();
        QCOMPARE(widget.resultBytes().value(),QByteArray::fromHex("efbbbf")); QVERIFY(!widget.isModified());
        widget.setEditable(false); QTest::keyClicks(area,"blocked"); QVERIFY(!widget.isModified());
    }
    void boundedUndoHistoryPreservesSidecarStates() {
        MergeWidget widget; load(widget,seed()); auto* area=widget.resultEditor()->edit()->area();
        for(int i=0;i<40;++i) QVERIFY(i%2 ? widget.markConflictUnresolved(0) : widget.markConflictResolved(0));
        QCOMPARE(area->undoStack()->count(),32);
        for(int i=0;i<32;++i) { area->undo(); QCOMPARE(widget.unresolvedCount(),i%2 ? 2 : 1); }
        for(int i=0;i<32;++i) { area->redo(); QCOMPARE(widget.unresolvedCount(),i%2 ? 2 : 1); }
    }
};
QTEST_MAIN(TestMergeEditing)
#include "test_merge_editing.moc"
