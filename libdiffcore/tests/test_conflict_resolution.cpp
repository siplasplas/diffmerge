#include <QTest>
#include <diffcore/ConflictMarkers.h>
using namespace diffcore;
class TestConflictResolution : public QObject {
    Q_OBJECT
private slots:
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
