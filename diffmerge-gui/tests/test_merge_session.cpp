#include <QTest>
#include <diffmerge/ConflictMarkers.h>
#include <future>
using namespace diffmerge::gui;
class TestMergeSession : public QObject {
    Q_OBJECT
    static MergeSessionInputs inputs(const QByteArray& result) {
        MergeSessionInputs inputs; MergeResultSeed seed;
        seed.file.availability = MergeAvailability::Present; seed.file.bytes = result;
        seed.origin = ResultSeedOrigin::RetainedResult; seed.fingerprint = "fingerprint";
        inputs.resultSeed = seed; return inputs;
    }
    static MarkerImportResult parse(const QByteArray& bytes, const MarkerImportOptions& options = {.allowUnconfirmedMarkers = true}) {
        const auto prepared = prepareMergeSession(inputs(bytes));
        if (!prepared.session) { MarkerImportResult result; result.status = prepared.status; return result; }
        return importConflictMarkers(*prepared.session, options);
    }
private slots:
    void preservesIndependentInputsAndSeed() {
        auto data = inputs(QByteArray::fromHex("efbbbf") + "manual\r\nretained\rfinal");
        data.base.availability = MergeAvailability::Absent;
        data.ours.availability = MergeAvailability::Present;
        data.ours.bytes = QByteArray(4095, 'a') + QByteArray::fromHex("f09f9880") + "\n";
        data.ours.rawPath = QByteArray("old/\xff.cpp", 9); data.ours.sourceId = "source"; data.ours.objectId = "object"; data.ours.mode = 0100755;
        data.theirs.availability = MergeAvailability::Present;
        data.hostConflicts = QVector<MergeConflict>{{.id="one", .result=diffcore::LineRange{1,1}}};
        const auto session = std::async(std::launch::async, [data] { return prepareMergeSession(data); }).get().session;
        QVERIFY(session); QVERIFY(!session->sourceText(MergeSource::Base));
        QVERIFY(session->sourceText(MergeSource::Theirs)); QVERIFY(session->sourceText(MergeSource::Theirs)->lines.isEmpty());
        QCOMPARE(session->sourceText(MergeSource::Ours)->lines[0].size(), 4097);
        QCOMPARE(session->inputs().resultSeed->file.bytes, data.resultSeed->file.bytes);
        QCOMPARE(session->inputs().resultSeed->fingerprint, QByteArray("fingerprint")); QVERIFY(session->resultHasUtf8Bom());
        QCOMPARE(session->resultText()->lineEndings, QVector<LineEnding>({LineEnding::CRLF,LineEnding::CR,LineEnding::None}));
        QCOMPARE(session->resultText()->finalNewline, std::optional<bool>(false));
        data.resultSeed->file.bytes[3] = 'X'; data.ours.rawPath[0] = 'X'; data.hostConflicts->clear();
        QVERIFY(session->inputs().resultSeed->file.bytes.contains("manual"));
        QCOMPARE(session->source(MergeSource::Ours).rawPath[0], 'o'); QCOMPARE(session->source(MergeSource::Ours).mode.value(), std::uint32_t(0100755));
        QCOMPARE(session->inputs().hostConflicts->size(), 1);
        QCOMPARE(session->inputs().hostConflicts->first().state, MergeResolutionState::Unresolved);
        MergeSessionInputs noSeed; const auto noResult = prepareMergeSession(noSeed); QVERIFY(noResult.session); QVERIFY(!noResult.session->resultText());
    }
    void preparationRejectsUnsafeAndInvalidInputs() {
        for (const auto& bytes : {QByteArray("a\0b",3), QByteArray::fromHex("e282"), QByteArray::fromHex("c0af")}) {
            const auto result = prepareMergeSession(inputs(bytes)); QCOMPARE(result.status, MergeSessionStatus::Unsupported); QVERIFY(!result.session);
        }
        auto data = inputs("text"); data.ours.availability = MergeAvailability::Present; data.ours.kind = MergeFileKind::SymbolicLink;
        QCOMPARE(prepareMergeSession(data).status, MergeSessionStatus::Unsupported);
        data.ours.kind = MergeFileKind::RegularFile; data.ours.availability = MergeAvailability::Unknown; data.ours.bytes = "placeholder";
        QCOMPARE(prepareMergeSession(data).status, MergeSessionStatus::Error);
        data.ours.bytes.clear(); data.hostConflicts = QVector<MergeConflict>{{.id="one", .result=diffcore::LineRange{0,2}}};
        QCOMPARE(prepareMergeSession(data).status, MergeSessionStatus::Error);
        data.hostConflicts = QVector<MergeConflict>{{.id="one", .base=diffcore::LineRange{0,0}}};
        QCOMPARE(prepareMergeSession(data).status, MergeSessionStatus::Error);
        data.hostConflicts = QVector<MergeConflict>{{.id="one"},{.id="one"}};
        QCOMPARE(prepareMergeSession(data).status, MergeSessionStatus::Error);
    }
    void limitsAndCancellation() {
        const auto data = inputs("first\nsecond\n"); MergeSessionLimits limits;
        limits.maxInputBytes = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        limits = {}; limits.maxInputLines = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        limits = {}; limits.maxInputCodeUnits = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        limits = {}; limits.maxLineCodeUnits = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        limits = {}; limits.maxWork = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        limits = {}; limits.maxMetadataBytes = 1; QCOMPARE(prepareMergeSession(data,limits).status, MergeSessionStatus::ResourceLimit);
        diffcore::CancellationToken token; token.requestCancellation();
        const auto cancelled = prepareMergeSession(data, {}, token); QCOMPARE(cancelled.status, MergeSessionStatus::Cancelled); QVERIFY(!cancelled.session);
        const auto session = prepareMergeSession(inputs("<<<<<<<\na\n=======\nb\n>>>>>>>\n")).session; QVERIFY(session);
        QCOMPARE(importConflictMarkers(*session, {.allowUnconfirmedMarkers=true}, {}, token).status, MergeSessionStatus::Cancelled);
        limits = {}; limits.maxConflicts = 0;
        auto imported = importConflictMarkers(*session, {.allowUnconfirmedMarkers=true}, limits);
        QCOMPARE(imported.status, MergeSessionStatus::ResourceLimit); QVERIFY(imported.conflicts.isEmpty());
    }
    void importsMultipleRegionsWithoutChangingBytes() {
        const QByteArray prefix = QByteArray::fromHex("efbbbf") + "common\r\n";
        const QByteArray first = "<<<<<<< ours\r\nold\r\n||||||| ancestor\r\nbase\r\n=======\r\nnew\r\n>>>>>>> theirs\r\n";
        const QByteArray common = "refined common text\n";
        const QByteArray second = "<<<<<<<\n\n=======\nother\n>>>>>>>";
        auto data = inputs(prefix + first + common + second);
        data.hostConflicts = QVector<MergeConflict>{{.id="host-one"},{.id="host-two", .state=MergeResolutionState::NeedsReview}};
        const auto session = prepareMergeSession(data).session; QVERIFY(session);
        const auto imported = importConflictMarkers(*session); QCOMPARE(imported.status, MergeSessionStatus::Ready); QCOMPARE(imported.conflicts.size(),2);
        const auto& one = imported.conflicts[0];
        QCOMPARE(one.resultBytes.start, qint64(prefix.size())); QCOMPARE(one.resultBytes.length, qint64(first.size()));
        QCOMPARE(one.resultLines.start,1); QCOMPARE(one.resultLines.count,7);
        QCOMPARE(one.ours,QByteArray("old\r\n")); QCOMPARE(one.base.value(),QByteArray("base\r\n")); QCOMPARE(one.theirs,QByteArray("new\r\n"));
        QCOMPARE(one.oursLabel,QString("ours")); QCOMPARE(one.baseLabel,QString("ancestor")); QCOMPARE(one.theirsLabel,QString("theirs"));
        QVERIFY(!imported.conflicts[1].base); QCOMPARE(imported.conflicts[1].ours,QByteArray("\n"));
        QCOMPARE(imported.conflicts[1].resultBytes.length,qint64(second.size()));
        QCOMPARE(session->inputs().resultSeed->file.bytes,data.resultSeed->file.bytes);
        QCOMPARE(session->inputs().hostConflicts->last().state,MergeResolutionState::NeedsReview);
        QVERIFY(!session->sourceText(MergeSource::Base));
    }
    void malformedMarkers_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("unfinished") << QByteArray("<<<<<<<\na\n=======\nb\n");
        QTest::newRow("nested") << QByteArray("<<<<<<<\na\n<<<<<<< nested\n=======\nb\n>>>>>>>\n");
        QTest::newRow("orphan") << QByteArray("=======\n");
        QTest::newRow("wrong-size") << QByteArray("<<<<<<<\na\n========\nb\n>>>>>>>\n");
        QTest::newRow("duplicate-base") << QByteArray("<<<<<<<\na\n|||||||\nx\n|||||||\n=======\nb\n>>>>>>>\n");
        QTest::newRow("bad-label") << QByteArray("<<<<<<<oops\na\n=======\nb\n>>>>>>>\n");
        QTest::newRow("separator-label") << QByteArray("<<<<<<<\na\n======= label\nb\n>>>>>>>\n");
        QTest::newRow("reversed") << QByteArray("<<<<<<<\na\n>>>>>>>\nb\n=======\n");
    }
    void malformedMarkers() {
        QFETCH(QByteArray,bytes);
        const auto imported = parse("<<<<<<<\nok\n=======\nalso ok\n>>>>>>>\n" + bytes);
        QCOMPARE(imported.status,MergeSessionStatus::Error); QVERIFY(imported.conflicts.isEmpty()); QVERIFY(!imported.message.isEmpty());
    }
    void consentLiteralLinesAndConfiguredSize() {
        auto data = inputs("<<<<<<< a\nx\n=======\ny\n>>>>>>> b\n");
        const auto session = prepareMergeSession(data).session; QVERIFY(session);
        QCOMPARE(importConflictMarkers(*session).status,MergeSessionStatus::Error);
        MarkerImportOptions literal; literal.literalMarkerLines={0,2,4};
        const auto untouched = importConflictMarkers(*session,literal); QCOMPARE(untouched.status,MergeSessionStatus::Ready); QVERIFY(untouched.conflicts.isEmpty());
        data.hostConflicts=QVector<MergeConflict>{{.id="still-unresolved"}}; data.resultSeed->file.bytes="manually edited without markers";
        const auto partial=prepareMergeSession(data).session;
        QCOMPARE(importConflictMarkers(*partial).status,MergeSessionStatus::Ready);
        QCOMPARE(partial->inputs().hostConflicts->first().state,MergeResolutionState::Unresolved);
        const auto sized=parse("<<<<<<<<<< a\rx\r|||||||||| b\r\r==========\ry\r>>>>>>>>>> c", {.markerSize=10,.allowUnconfirmedMarkers=true});
        QCOMPARE(sized.status,MergeSessionStatus::Ready); QCOMPARE(sized.conflicts.size(),1); QVERIFY(sized.conflicts[0].base);
        QCOMPARE(sized.conflicts[0].base.value(),QByteArray("\r"));
    }
};
QTEST_GUILESS_MAIN(TestMergeSession)
#include "test_merge_session.moc"
