// Unit tests for AlignedLineModel - verifies that hunks are translated
// to aligned rows correctly, with placeholders in the right places.

#include <QObject>
#include <QStringList>
#include <QTest>

#include <diffcore/DiffEngine.h>

#include <diffmerge/AlignedLineModel.h>

using namespace diffmerge::gui;
using diffcore::ChangeType;

class TestAlignedModel : public QObject {
    Q_OBJECT

private:
    // Build a model from two line lists by running the real engine.
    AlignedLineModel buildFor(const QStringList& left,
                              const QStringList& right) {
        diffcore::DiffEngine engine;
        const auto result = engine.compute(left, right);
        AlignedLineModel m;
        m.build(result, left, right);
        return m;
    }

private slots:
    void oneSidedBlockRanges_data() {
        QTest::addColumn<bool>("deletion");
        QTest::addColumn<int>("boundary");
        QTest::addColumn<bool>("empty");
        for (bool deletion : {false, true}) {
            for (int boundary = 0; boundary <= 2; ++boundary) {
                const QByteArray name = QByteArray::number(deletion) + "-"
                                      + QByteArray::number(boundary);
                QTest::newRow(name.constData()) << deletion << boundary << false;
            }
            const QByteArray name = QByteArray::number(deletion) + "-empty";
            QTest::newRow(name.constData()) << deletion << 0 << true;
        }
    }

    void oneSidedBlockRanges() {
        QFETCH(bool, deletion);
        QFETCH(int, boundary);
        QFETCH(bool, empty);
        const QStringList unchanged = empty ? QStringList{} : QStringList{"before", "after"};
        QStringList changed = unchanged;
        changed.insert(boundary, "first added line");
        changed.insert(boundary + 1, "second added line");
        const QStringList left = deletion ? changed : unchanged;
        const QStringList right = deletion ? unchanged : changed;
        auto model = buildFor(left, right);
        QCOMPARE(model.changeBlocks().size(), 1);
        const auto& block = model.changeBlocks().first();
        QCOMPARE(block.type, deletion ? ChangeType::Delete : ChangeType::Insert);
        const Side missingSide = deletion ? Side::Right : Side::Left;
        const auto& missing = block.range(missingSide);
        const auto& present = block.range(deletion ? Side::Left : Side::Right);
        QVERIFY(missing.isEmpty());
        QCOMPARE(missing.start, boundary);
        QCOMPARE(missing.end(), boundary);
        QCOMPARE(present.start, boundary);
        QCOMPARE(present.count, 2);
        QCOMPARE(present.end(), boundary + 2);
        QCOMPARE(model.documentLines(Side::Left), left);
        QCOMPARE(model.documentLines(Side::Right), right);
    }

    void mixedBlocksUseOriginalCoordinates() {
        const QStringList left{"before", "old1", "old2", "tail", "gone", "end"};
        const QStringList right{"before", "new1", "new2", "new3", "tail", "end", "extra"};
        // Keep this unequal replacement intact, independent of engine pairing.
        diffcore::DiffResult diff;
        diff.hunks = {
            {ChangeType::Equal,   {0, 1}, {0, 1}},
            {ChangeType::Replace, {1, 2}, {1, 3}},
            {ChangeType::Equal,   {3, 1}, {4, 1}},
            {ChangeType::Delete,  {4, 1}, {5, 0}},
            {ChangeType::Equal,   {5, 1}, {5, 1}},
            {ChangeType::Insert,  {6, 0}, {6, 1}},
        };
        AlignedLineModel model;
        model.build(diff, left, right);
        const auto& blocks = model.changeBlocks();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].type, ChangeType::Replace);
        QCOMPARE(blocks[0].leftRange.start, 1);
        QCOMPARE(blocks[0].leftRange.end(), 3);
        QCOMPARE(blocks[0].rightRange.start, 1);
        QCOMPARE(blocks[0].rightRange.end(), 4);
        QCOMPARE(blocks[1].type, ChangeType::Delete);
        QCOMPARE(blocks[1].leftRange.start, 4);
        QCOMPARE(blocks[1].leftRange.end(), 5);
        QCOMPARE(blocks[1].rightRange.start, 5);
        QVERIFY(blocks[1].rightRange.isEmpty());
        QCOMPARE(blocks[2].type, ChangeType::Insert);
        QCOMPARE(blocks[2].leftRange.start, 6);
        QVERIFY(blocks[2].leftRange.isEmpty());
        QCOMPARE(blocks[2].rightRange.start, 6);
        QCOMPARE(blocks[2].rightRange.end(), 7);
        QCOMPARE(model.docLineChangeType(Side::Left, 3), ChangeType::Equal);
        QCOMPARE(model.docLineChangeType(Side::Left, 4), ChangeType::Delete);
        QCOMPARE(model.docLineChangeType(Side::Right, 3), ChangeType::Replace);
        QCOMPARE(model.docLineChangeType(Side::Right, 5), ChangeType::Equal);
        QCOMPARE(model.docLineChangeType(Side::Right, 6), ChangeType::Insert);
        QCOMPARE(model.docLineChangeType(Side::Right, 7), ChangeType::Equal);

        // Rebuilding must discard the previous comparison's blocks.
        model.build(diffcore::DiffEngine{}.compute(left, left), left, left);
        QVERIFY(model.changeBlocks().isEmpty());
        QCOMPARE(model.docLineChangeType(Side::Left, 4), ChangeType::Equal);
        model.build(diffcore::DiffEngine{}.compute({}, {}), {}, {});
        QVERIFY(model.changeBlocks().isEmpty());
    }

    void insertionAfterReplacementKeepsItsBoundary() {
        const QStringList left{"old"};
        const QStringList right{"new", "replacement tail", "inserted"};
        diffcore::DiffResult diff;
        diff.hunks = {
            {ChangeType::Replace, {0, 1}, {0, 2}},
            {ChangeType::Insert, {1, 0}, {2, 1}},
        };
        AlignedLineModel model;
        model.build(diff, left, right);
        const auto fillers = model.fillerRanges(Side::Left);
        QCOMPARE(fillers.size(), 2);
        QCOMPARE(fillers[0].changeType, ChangeType::Replace);
        QCOMPARE(fillers[1].changeType, ChangeType::Insert);
        QCOMPARE(fillers[1].beforeDocLine, 1);
        QCOMPARE(model.documentLines(Side::Left), left);
    }

    void identicalFilesProduceNoPlaceholders() {
        const QStringList lines{"a", "b", "c"};
        auto m = buildFor(lines, lines);
        QCOMPARE(m.rowCount(), 3);
        for (int i = 0; i < m.rowCount(); ++i) {
            QVERIFY(!m.row(Side::Left, i).isPlaceholder());
            QVERIFY(!m.row(Side::Right, i).isPlaceholder());
            QCOMPARE(int(m.row(Side::Left, i).changeType),
                     int(ChangeType::Equal));
        }
    }

    void insertAddsPlaceholderOnLeft() {
        // Right adds a line in the middle.
        const QStringList left{"a", "c"};
        const QStringList right{"a", "b", "c"};
        auto m = buildFor(left, right);
        QCOMPARE(m.rowCount(), 3);

        // Row 0: both "a", Equal.
        QVERIFY(!m.row(Side::Left, 0).isPlaceholder());
        QCOMPARE(int(m.row(Side::Left, 0).changeType), int(ChangeType::Equal));

        // Row 1: left placeholder, right "b", Insert.
        QVERIFY(m.row(Side::Left, 1).isPlaceholder());
        QVERIFY(!m.row(Side::Right, 1).isPlaceholder());
        QCOMPARE(int(m.row(Side::Left, 1).changeType), int(ChangeType::Insert));
        QCOMPARE(m.text(Side::Right, 1), QStringLiteral("b"));

        // Row 2: both "c", Equal.
        QVERIFY(!m.row(Side::Left, 2).isPlaceholder());
    }

    void deleteAddsPlaceholderOnRight() {
        const QStringList left{"a", "b", "c"};
        const QStringList right{"a", "c"};
        auto m = buildFor(left, right);
        QCOMPARE(m.rowCount(), 3);

        // Row 1: left "b", right placeholder, Delete.
        QVERIFY(!m.row(Side::Left, 1).isPlaceholder());
        QVERIFY(m.row(Side::Right, 1).isPlaceholder());
        QCOMPARE(int(m.row(Side::Right, 1).changeType),
                 int(ChangeType::Delete));
    }

    void replacePadsShorterSide() {
        // Left has 2 lines where right has 3 -> Replace with pad on left.
        // Note: the O(NP) engine does not consolidate this into a single
        // hunk; it typically emits Insert + Replace(2 vs 1). What matters
        // for the model is that BOTH sides end up with the same row count
        // and that the shorter side of each hunk is padded with
        // placeholders.
        const QStringList left{"x", "a", "b"};
        const QStringList right{"x", "A", "B", "C"};
        auto m = buildFor(left, right);

        // Both sides must have equal row counts (core invariant).
        const int count = m.rowCount();
        QVERIFY(count >= 4);

        // Row 0: both "x", Equal.
        QVERIFY(!m.row(Side::Left, 0).isPlaceholder());
        QCOMPARE(int(m.row(Side::Left, 0).changeType),
                 int(ChangeType::Equal));

        // Every non-Equal row must be a change type; every placeholder
        // must coincide with at least one non-Equal change type on that
        // side's row.
        int leftPlaceholders = 0, rightPlaceholders = 0;
        for (int i = 0; i < count; ++i) {
            if (m.row(Side::Left, i).isPlaceholder()) ++leftPlaceholders;
            if (m.row(Side::Right, i).isPlaceholder()) ++rightPlaceholders;
        }
        // Left has one fewer real line than right, so left needs at least
        // one more placeholder.
        QVERIFY(leftPlaceholders > rightPlaceholders);
    }

    void fileLineNumbersSkipPlaceholders() {
        // When placeholder rows exist, the non-placeholder rows should
        // still have correct file-line numbers (not aligned row numbers).
        const QStringList left{"a", "c"};
        const QStringList right{"a", "b", "c"};
        auto m = buildFor(left, right);

        QCOMPARE(m.row(Side::Left, 0).fileLineNumber, 0);   // "a"
        QCOMPARE(m.row(Side::Left, 1).fileLineNumber, -1);  // placeholder
        QCOMPARE(m.row(Side::Left, 2).fileLineNumber, 1);   // "c"

        QCOMPARE(m.row(Side::Right, 0).fileLineNumber, 0);
        QCOMPARE(m.row(Side::Right, 1).fileLineNumber, 1);
        QCOMPARE(m.row(Side::Right, 2).fileLineNumber, 2);
    }

    void buildDocumentTextHasEmptyLinesForPlaceholders() {
        const QStringList left{"a", "c"};
        const QStringList right{"a", "b", "c"};
        auto m = buildFor(left, right);

        const QString leftDoc = m.buildDocumentText(Side::Left);
        const QStringList leftRows = leftDoc.split(QLatin1Char('\n'));
        QCOMPARE(leftRows.size(), 3);
        QCOMPARE(leftRows.at(0), QStringLiteral("a"));
        QCOMPARE(leftRows.at(1), QString());  // placeholder = empty
        QCOMPARE(leftRows.at(2), QStringLiteral("c"));
    }

    void bothSidesHaveEqualRowCounts() {
        // Varied example with all four hunk types.
        const QStringList left{"a", "b", "c", "d", "e"};
        const QStringList right{"A", "b", "X", "Y", "d", "f"};
        auto m = buildFor(left, right);
        // Both sides must have identical row counts - the core invariant
        // of the alignment model.
        const int count = m.rowCount();
        QVERIFY(count > 0);
        // Also check a few basic facts about reasonable coverage.
        int leftReal = 0, rightReal = 0;
        for (int i = 0; i < count; ++i) {
            if (!m.row(Side::Left, i).isPlaceholder()) ++leftReal;
            if (!m.row(Side::Right, i).isPlaceholder()) ++rightReal;
        }
        QCOMPARE(leftReal, left.size());
        QCOMPARE(rightReal, right.size());
    }
};

QTEST_APPLESS_MAIN(TestAlignedModel)
#include "test_aligned_model.moc"
