#include <QTest>
#include <QJsonArray>
#include <algorithm>
#include <diffcore/ConflictResolution.h>
using namespace diffcore;
class TestConflictResolution : public QObject {
    Q_OBJECT
    static QByteArray block(const QByteArray& left,const QByteArray& base,const QByteArray& right) {
        return "<<<<<<< left\n"+left+"||||||| base\n"+base+"=======\n"+right+">>>>>>> right\n";
    }
private slots:
    void compositionAndBytes() {
        const auto input=QByteArray::fromHex("efbbbf")+"head\r\n"+block("guard();\n\nold();\r\n","old();\r\n","new();\n")+"tail";
        const auto plan=planConflictResolution(input);
        QCOMPARE(plan.status,ConflictStatus::Complete); QCOMPARE(plan.decisions[0].rule,QString("prefix-plus-replacement"));
        const auto out=materializeResolution(plan); QVERIFY(out.clean);
        QCOMPARE(out.bytes,QByteArray::fromHex("efbbbf")+"head\r\nguard();\n\nnew();\ntail");
        QCOMPARE(materializeResolution(planConflictResolution(out.bytes)).bytes,out.bytes);
        const auto independent=planConflictResolution(block("A\nb\nc\n","a\nb\nc\n","a\nb\nC\n"));
        QVERIFY(materializeResolution(independent).clean); QCOMPARE(materializeResolution(independent).bytes,QByteArray("A\nb\nC\n"));
    }
    void policyDeletionAndReview() {
        const auto input=block("updated();\n","old();\n","");
        auto plan=planConflictResolution(input); QCOMPARE(plan.decisions[0].state,DecisionState::AutomaticPolicy);
        QCOMPARE(materializeResolution(plan).bytes,QByteArray());
        ResolutionOptions conservative; conservative.policy=ResolutionPolicy::Conservative;
        plan=planConflictResolution(input,{},conservative); QCOMPARE(plan.decisions[0].state,DecisionState::NeedsReview);
        QCOMPARE(materializeResolution(plan).bytes,input);
        const auto draft=materializeResolution(plan,true); QVERIFY(!draft.clean); QCOMPARE(draft.bytes,QByteArray()); QCOMPARE(draft.deferredIds.size(),1);
        QVERIFY(!reviewConflictDecision(plan,"stale",plan.decisions[0].id,""));
        QVERIFY(reviewConflictDecision(plan,plan.input.sha256,plan.decisions[0].id,"")); QVERIFY(materializeResolution(plan).clean);
    }
    void policyRewriteAndIndependentAddition() {
        const QByteArray base="Library alpha needs release 2.0 and must be installed before building the application. Run the installer and configure the application afterwards. This requirement applies to all supported systems.\n";
        auto left=base; left.replace("2.0","2.5 or later");
        const QByteArray right="FetchContent obtains pinned dependencies automatically.\n";
        auto plan=planConflictResolution(block(left,base,right));
        QCOMPARE(plan.decisions[0].rule,QString("target-rewrite-supersedes-edits")); QCOMPARE(materializeResolution(plan).bytes,right);
        plan=planConflictResolution(block("independent();\n"+left,base,right));
        QCOMPARE(plan.decisions[0].state,DecisionState::NeedsReview);
    }
    void movedStatementCandidate() {
        const QByteArray base="int oldIndex = addTab(newEditor);\nint removed = enforceLimit();\nint finalIndex = oldIndex - removed;\n";
        const QByteArray left="int finalIndex = addTab(newEditor);\n";
        const QByteArray right="extract();\n}\nvoid helper(Editor* editor) {\nint oldIndex = addTab(editor);\nint removed = enforceLimit();\nint finalIndex = oldIndex - removed;\n";
        const auto plan=planConflictResolution(block("context();\n"+left,"context();\n"+base,right));
        QCOMPARE(plan.decisions[0].state,DecisionState::NeedsReview); QVERIFY(!plan.decisions[0].candidates.isEmpty());
        const auto c=plan.decisions[0].candidates[0]; QCOMPARE(c.id,QString("adapted"));
        QCOMPARE(c.replacement,QByteArray("extract();\n}\nvoid helper(Editor* editor) {\nint finalIndex = addTab(editor);\n"));
        QVERIFY(!c.replacement.contains("addTab(newEditor)"));
        const auto& d=plan.decisions[0]; QVERIFY(d.reviewPresentation); QCOMPARE(d.reviewPresentation->candidateIds.size(),3);
        QCOMPARE(d.reviewPresentation->sourceIdentifier,QString("newEditor")); QCOMPARE(d.reviewPresentation->targetIdentifier,QString("editor"));
        for(const auto& candidate:d.candidates) if(d.reviewPresentation->candidateIds.contains(candidate.id)) {
            QVERIFY(candidate.focusRange);
            const auto r=*candidate.focusRange;
            QCOMPARE(candidate.replacement.left(r.start),d.reviewPresentation->prefix);
            QCOMPARE(candidate.replacement.mid(r.end()),d.reviewPresentation->suffix);
        }
        const auto replayed=std::find_if(d.candidates.cbegin(),d.candidates.cend(),[](const auto& item) { return item.id=="replayed"; });
        QVERIFY(replayed!=d.candidates.cend()); QVERIFY(replayed->replacement.contains("int finalIndex = addTab(newEditor);"));
        ResolutionOptions labels; labels.sourceLabels={"main","ancestor","codeedit"};
        const auto named=planConflictResolution(block(left,base,right),{},labels);
        const auto report=resolutionReport(named,materializeResolution(named));
        QCOMPARE(report["sourceLabels"].toObject()["left"].toString(),QString("main"));
        QCOMPARE(report["conflicts"].toArray()[0].toObject()["labels"].toObject()["left"].toString(),QString("left"));
    }
    void ambiguousAndSameGap() {
        auto plan=planConflictResolution(block("x\na\n","a\n","y\na\n")); QVERIFY(!materializeResolution(plan).clean);
        plan=planConflictResolution("<<<<<<<\nleft\n=======\nright\n>>>>>>>\n");
        QVERIFY(!materializeResolution(plan).clean); QCOMPARE(plan.decisions[0].reasons[0],QString("missing-base"));
        ConflictLimits limits; limits.maxOutputBytes=1;
        QCOMPARE(materializeResolution(plan,false,limits).status,ConflictStatus::ResourceLimit);
        CancellationToken token; token.requestCancellation();
        QCOMPARE(planConflictResolution(block("a\n","b\n","c\n"),{},{},{},token).status,ConflictStatus::Cancelled);
    }
    void rejectsStaleOrIncompletePlans() {
        auto plan=planConflictResolution(block("same\n","old\n","same\n"));
        plan.decisions[0].replacement.reset();
        QCOMPARE(materializeResolution(plan).status,ConflictStatus::Error);
        plan=planConflictResolution(block("same\n","old\n","same\n"));
        plan.input.bytes.append("changed");
        QCOMPARE(materializeResolution(plan).status,ConflictStatus::Error);
    }
    void parserPreservesRanges() {
        const QByteArray input=QByteArray::fromHex("efbbbf")+"context\r\n<<<<<<< left\r\nold\r\n||||||| ancestor\n=======\rnew\n>>>>>>> right";
        const auto parsed=parseConflictFile(input);
        QCOMPARE(parsed.status,ConflictStatus::Complete); QCOMPARE(parsed.file.conflicts.size(),1);
        const auto c=parsed.file.conflicts[0]; QCOMPARE(c.envelope.start,12); QVERIFY(c.base);
        QCOMPARE(c.base->length,0); QCOMPARE(input.mid(c.left.start,c.left.length),QByteArray("old\r\n"));
        QCOMPARE(input.mid(c.right.start,c.right.length),QByteArray("new\n"));
        QCOMPARE(c.envelope.end(),input.size());
        QCOMPARE(parseConflictFile(input).file.conflicts[0].id,c.id);
    }
    void malformedAndMissingBase() {
        for(const auto& input:{QByteArray("<<<<<<<\na\n"),QByteArray("<<<<<<<\na\n<<<<<<<\nb\n=======\nc\n>>>>>>>\n"),QByteArray("=======\n")}) {
            const auto parsed=parseConflictFile(input); QCOMPARE(parsed.status,ConflictStatus::InvalidInput); QVERIFY(parsed.file.conflicts.isEmpty());
        }
        const auto parsed=parseConflictFile("<<<<<<<\n=======\n>>>>>>>\n");
        QCOMPARE(parsed.status,ConflictStatus::Complete); QVERIFY(!parsed.file.conflicts[0].base);
        QCOMPARE(parseConflictFile(QByteArray("\xff",1)).status,ConflictStatus::Unsupported);
        QCOMPARE(parseConflictFile(QByteArray("\xf0\x9f",2)).status,ConflictStatus::Unsupported);
    }
    void parserBudgetsAndLiterals() {
        ConflictLimits limits; limits.maxInputBytes=1;
        QCOMPARE(parseConflictFile("abc",{},limits).status,ConflictStatus::ResourceLimit);
        CancellationToken token; token.requestCancellation();
        QCOMPARE(parseConflictFile("abc",{}, {},token).status,ConflictStatus::Cancelled);
        QCOMPARE(parseConflictFile("=======\n",{7,{0}}).status,ConflictStatus::Complete);
        QCOMPARE(parseConflictFile("=======\n",{7,{1}}).status,ConflictStatus::InvalidInput);
    }
};
QTEST_GUILESS_MAIN(TestConflictResolution)
#include "test_conflict_resolution.moc"
