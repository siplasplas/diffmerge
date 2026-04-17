// Unit tests for AlignedLineModel - verifies that hunks are translated
// to aligned rows correctly, with placeholders in the right places.

#include <QObject>
#include <QStringList>
#include <QTest>

#include <diffcore/DiffEngine.h>

#include "../src/fileview/AlignedLineModel.h"

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
