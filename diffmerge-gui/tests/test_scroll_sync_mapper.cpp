// Unit tests for ScrollSyncMapper.

#include <QObject>
#include <QTest>

#include <diffcore/DiffEngine.h>

#include "../src/fileview/AlignedLineModel.h"
#include "../src/fileview/ScrollSyncMapper.h"

using namespace diffmerge::gui;

class TestScrollSyncMapper : public QObject {
    Q_OBJECT

private:
    // Build mapper from two line lists using the real diff engine.
    ScrollSyncMapper buildFor(const QStringList& left, const QStringList& right,
                              double threshold = 0.0) {
        diffcore::DiffEngine engine;
        const auto result = engine.compute(left, right);
        ScrollSyncMapper m;
        m.build(result);
        m.setThreshold(threshold);
        return m;
    }

    // Shorthand: threshold=0 means syncLine == topLine, so
    // computeOtherTop(side, top, viewportLines=0, ...) maps top directly.
    int mapLeft(const ScrollSyncMapper& m, int top,
                int viewport, int otherCount) {
        return m.computeOtherTop(Side::Left, top, viewport, otherCount);
    }
    int mapRight(const ScrollSyncMapper& m, int top,
                 int viewport, int otherCount) {
        return m.computeOtherTop(Side::Right, top, viewport, otherCount);
    }

private slots:

    // --- Identical files: direct 1:1 mapping ---

    void identicalFiles_leftToRight() {
        const QStringList lines{"a", "b", "c", "d", "e"};
        auto m = buildFor(lines, lines);
        QCOMPARE(mapLeft(m, 0, 0, 5), 0);
        QCOMPARE(mapLeft(m, 2, 0, 5), 2);
        QCOMPARE(mapLeft(m, 4, 0, 5), 4);
    }

    void identicalFiles_rightToLeft() {
        const QStringList lines{"a", "b", "c"};
        auto m = buildFor(lines, lines);
        QCOMPARE(mapRight(m, 0, 0, 3), 0);
        QCOMPARE(mapRight(m, 2, 0, 3), 2);
    }

    // --- Insert on right: left stays at boundary while right scrolls ---

    void insertOnRight_leftScrolls_rightJumpsOverInsert() {
        // Left:  a b c      (3 lines, doc lines 0-2)
        // Right: a b X Y c  (5 lines, X Y inserted between b and c)
        // Equal a,b → left[0..2) = right[0..2)
        // Insert X,Y → right[2..4)
        // Equal c → left[2..3) = right[4..5)

        const QStringList left{"a", "b", "c"};
        const QStringList right{"a", "b", "X", "Y", "c"};
        auto m = buildFor(left, right);

        // Left top=0 → right top=0 (both at 'a')
        QCOMPARE(mapLeft(m, 0, 0, 5), 0);
        // Left top=1 → right top=1 (both at 'b')
        QCOMPARE(mapLeft(m, 1, 0, 5), 1);
        // Left top=2 → right top=4 (left at 'c', right jumped past X,Y to 'c')
        QCOMPARE(mapLeft(m, 2, 0, 5), 4);
    }

    void insertOnRight_rightScrollsThroughInsert_leftStaysAtBoundary() {
        // When user scrolls RIGHT through X,Y, left should stay at boundary (=2, start of 'c').
        const QStringList left{"a", "b", "c"};
        const QStringList right{"a", "b", "X", "Y", "c"};
        auto m = buildFor(left, right);

        // Right top=2 (at X): left should map to 2 (start of 'c' on left = the boundary)
        QCOMPARE(mapRight(m, 2, 0, 3), 2);
        // Right top=3 (at Y): left still at boundary
        QCOMPARE(mapRight(m, 3, 0, 3), 2);
        // Right top=4 (at c): back in sync, left=2 ('c')
        QCOMPARE(mapRight(m, 4, 0, 3), 2);
    }

    // --- Delete on left: right stays at boundary while left scrolls ---

    void deleteOnLeft_rightScrolls_leftJumpsOverDelete() {
        // Left:  a b X Y c  (5 lines)
        // Right: a b c      (3 lines, X Y deleted)
        const QStringList left{"a", "b", "X", "Y", "c"};
        const QStringList right{"a", "b", "c"};
        auto m = buildFor(left, right);

        // Right top=2 → left top=4 (right at 'c', left jumped past X,Y to 'c')
        QCOMPARE(mapRight(m, 2, 0, 5), 4);
    }

    void deleteOnLeft_leftScrollsThroughDelete_rightStaysAtBoundary() {
        const QStringList left{"a", "b", "X", "Y", "c"};
        const QStringList right{"a", "b", "c"};
        auto m = buildFor(left, right);

        // Left top=2 (at X): right maps to 2 (boundary = start of 'c' on right)
        QCOMPARE(mapLeft(m, 2, 0, 3), 2);
        // Left top=3 (at Y): right still at boundary
        QCOMPARE(mapLeft(m, 3, 0, 3), 2);
    }

    // --- Replace equal height: 1:1 ---

    void replaceEqualHeight_mapsOneToOne() {
        // Left:  a X b  (X replaced by Y)
        // Right: a Y b
        const QStringList left{"a", "X", "b"};
        const QStringList right{"a", "Y", "b"};
        auto m = buildFor(left, right);

        QCOMPARE(mapLeft(m, 1, 0, 3), 1);
        QCOMPARE(mapRight(m, 1, 0, 3), 1);
    }

    // --- Replace unequal height: proportional ---

    void replaceUnequalHeight_proportionalMapping() {
        // Left:  a X1 X2 X3 b  (3 replaced lines)
        // Right: a Y1 Y2 b     (2 replaced lines)
        // Replace: left[1..4) → right[1..3)
        const QStringList left{"a", "X1", "X2", "X3", "b"};
        const QStringList right{"a", "Y1", "Y2", "b"};
        auto m = buildFor(left, right);

        // Left top=1 → right top=1 (start of replace, 0/3 fraction → 0/2 → 1)
        QCOMPARE(mapLeft(m, 1, 0, 4), 1);
        // Left top=4 (at 'b') → right top=3 (at 'b')
        QCOMPARE(mapLeft(m, 4, 0, 4), 3);
    }

    // --- Threshold effect ---

    void threshold_shiftsSyncLine() {
        // Identical files, threshold=0.5, viewport=4 lines.
        // syncLine = top + 0.5*4 = top+2
        // correspondingLine for Equal is same → otherTop = syncLine - 0.5*4 = top
        // So result should equal top (offset cancels out for Equal)
        const QStringList lines{"a", "b", "c", "d", "e"};
        auto m = buildFor(lines, lines, 0.5);
        QCOMPARE(m.computeOtherTop(Side::Left, 0, 4, 5), 0);
        QCOMPARE(m.computeOtherTop(Side::Left, 2, 4, 5), 2);
    }

    void threshold_insertOnRight_rightStaysWhileLeftScrolls() {
        // Left: a b c  Right: a b X Y Z c
        // With threshold=0 AND viewport=0, the sync line equals topLine.
        // At left top=2 ('c'), right should jump to 5 ('c' on right).
        const QStringList left{"a", "b", "c"};
        const QStringList right{"a", "b", "X", "Y", "Z", "c"};
        auto m = buildFor(left, right, 0.0);

        QCOMPARE(mapLeft(m, 2, 0, 6), 5);

        // With threshold=0.5 and viewport=4: syncLine at left top=0 is 0+0.5*4=2.
        // correspondingLine(Left, 2) = 5 (start of 'c' on right).
        // otherTop = 5 - 0.5*4 = 3.
        auto m2 = buildFor(left, right, 0.5);
        QCOMPARE(m2.computeOtherTop(Side::Left, 0, 4, 6), 3);
    }

    // --- Clamping ---

    void result_clampedToZero() {
        const QStringList lines{"a", "b", "c"};
        auto m = buildFor(lines, lines);
        QCOMPARE(mapLeft(m, 0, 0, 3), 0);
    }

    void result_clampedToDocLineCount() {
        const QStringList lines{"a", "b", "c"};
        auto m = buildFor(lines, lines);
        // Requesting past end clamps to otherDocLineCount-1 = 2.
        QCOMPARE(m.computeOtherTop(Side::Left, 100, 0, 3), 2);
    }
};

QTEST_MAIN(TestScrollSyncMapper)
#include "test_scroll_sync_mapper.moc"
