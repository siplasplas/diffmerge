#include <QTest>
#include <QSignalSpy>
#include <QToolButton>
#include <diffmerge/MergePreviewWidget.h>
#include <diffmerge/DiffEditor.h>
#include <qce/CodeEditArea.h>
using namespace diffmerge::gui;
class TestMergePreview : public QObject {
    Q_OBJECT
    static std::shared_ptr<const PreparedMergeSession> session(const QByteArray& bytes, bool known = false) {
        MergeSessionInputs inputs; MergeResultSeed seed; seed.file.availability=MergeAvailability::Present; seed.file.bytes=bytes;
        inputs.resultSeed=seed;
        if(known) { inputs.ours.availability=MergeAvailability::Present; inputs.ours.bytes="whole ours\n"; inputs.ours.label="source label"; }
        return prepareMergeSession(inputs).session;
    }
private slots:
    void readonlyNavigationAndFragmentIdentity() {
        const QByteArray bytes="prefix\n<<<<<<<\nfirst ours\n|||||||\nfirst base\n=======\nfirst theirs\n>>>>>>>\nmiddle\n<<<<<<<\nsecond ours\n=======\nsecond theirs\n>>>>>>>\nsuffix";
        const auto prepared=session(bytes); QVERIFY(prepared);
        MergePreviewWidget widget; widget.resize(1100,500); widget.show(); QTest::qWait(10); QCOMPARE(widget.panelSpacing(),24); QVERIFY(!widget.baseVisible());
        QVERIFY(widget.setSession(prepared,{.allowUnconfirmedMarkers=true})); QCOMPARE(widget.markerConflicts().size(),2); QCOMPARE(widget.currentConflictIndex(),0);
        auto* result=widget.resultEditor()->edit()->area(); QCOMPARE(result->document()->lineAt(0),QString("prefix"));
        QCOMPARE(result->document()->lineAt(result->document()->lineCount()-1),QString("suffix"));
        for(auto source:{MergeSource::Base,MergeSource::Ours,MergeSource::Theirs}) QVERIFY(widget.sourceEditor(source)->edit()->area()->readOnly());
        QVERIFY(result->readOnly()); QCOMPARE(widget.sourceEditor(MergeSource::Ours)->edit()->area()->document()->lineAt(0),QString("first ours"));
        QCOMPARE(widget.sourceEditor(MergeSource::Base)->edit()->area()->document()->lineAt(0),QString("first base"));
        widget.setBaseVisible(true); QVERIFY(widget.baseVisible());
        auto* baseButton=widget.findChild<QToolButton*>("showMergeBase"); QVERIFY(baseButton); QVERIFY(baseButton->isChecked());
        baseButton->click(); QVERIFY(!widget.baseVisible());
        widget.navigateToNextConflict(); QCOMPARE(widget.currentConflictIndex(),1);
        QCOMPARE(result->cursorPosition().line, widget.markerConflicts()[1].resultLines.start);
        QCOMPARE(widget.sourceEditor(MergeSource::Ours)->edit()->area()->document()->lineAt(0),QString("second ours"));
        QVERIFY(!widget.navigateToConflict(20)); QCOMPARE(widget.currentConflictIndex(),1);
        widget.navigateToPreviousConflict(); QCOMPARE(widget.currentConflictIndex(),0);
        QCOMPARE(prepared->inputs().resultSeed->file.bytes,bytes); QVERIFY(!prepared->sourceText(MergeSource::Base));
        widget.setPanelSpacing(1); QCOMPARE(widget.panelSpacing(),8); widget.setPanelSpacing(200); QCOMPARE(widget.panelSpacing(),160);
    }
    void failedReplacementKeepsPreviousSessionAndKnownSources() {
        const QByteArray bytes="<<<<<<<\nfragment\n=======\nother\n>>>>>>>\n";
        MergePreviewWidget widget; const auto original=session(bytes,true); QVERIFY(original);
        QVERIFY(widget.setSession(original,{.allowUnconfirmedMarkers=true}));
        QCOMPARE(widget.sourceEditor(MergeSource::Ours)->edit()->area()->document()->lineAt(0),QString("whole ours"));
        QSignalSpy failed(&widget,&MergePreviewWidget::operationFailed);
        QVERIFY(!widget.setSession(session("<<<<<<<\nincomplete"),{.allowUnconfirmedMarkers=true})); QCOMPARE(failed.size(),1);
        QCOMPARE(widget.session(),original); QCOMPARE(widget.markerConflicts().size(),1);
        QVERIFY(widget.setSession(nullptr)); QVERIFY(!widget.session()); QCOMPARE(widget.currentConflictIndex(),-1);
        QVERIFY(!widget.navigateToConflict(0));
    }
};
QTEST_MAIN(TestMergePreview)
#include "test_merge_preview.moc"
