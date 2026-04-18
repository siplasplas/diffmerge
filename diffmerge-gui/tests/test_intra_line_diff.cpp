// Unit tests for IntraLineDiffEngine.

#include <QObject>
#include <QTest>

#include <diffcore/DiffEngine.h>

#include "../src/editor/IntraLineDiffEngine.h"

using namespace diffmerge::gui;
using Range = IntraLineDiffEngine::CharRange;

class TestIntraLineDiff : public QObject {
    Q_OBJECT

private:
    IntraLineDiffEngine::Result build(const QStringList& left,
                                      const QStringList& right) {
        diffcore::DiffEngine engine;
        const auto diff = engine.compute(left, right);
        return IntraLineDiffEngine::compute(diff, left, right);
    }

    static bool hasRange(const QVector<Range>& ranges, int start, int len) {
        for (const auto& r : ranges)
            if (r.start == start && r.length == len) return true;
        return false;
    }

private slots:

    // No Replace hunks → all range lists empty.
    void noReplaceHunks_noCharHighlights() {
        const QStringList left{"a", "b"};
        const QStringList right{"a", "b", "c"};  // pure insert
        auto res = build(left, right);
        for (const auto& v : res.leftRanges)  QVERIFY(v.isEmpty());
        for (const auto& v : res.rightRanges) QVERIFY(v.isEmpty());
    }

    // Identical files → no highlights.
    void identicalFiles_noHighlights() {
        const QStringList lines{"hello world", "foo bar"};
        auto res = build(lines, lines);
        for (const auto& v : res.leftRanges)  QVERIFY(v.isEmpty());
        for (const auto& v : res.rightRanges) QVERIFY(v.isEmpty());
    }

    // Single char change in the middle: "abc" → "axc".
    void singleCharChange_highlightsOnlyChangedChar() {
        const QStringList left{"abc"};
        const QStringList right{"axc"};
        auto res = build(left, right);

        QCOMPARE(res.leftRanges.size(), 1);
        QCOMPARE(res.rightRanges.size(), 1);
        // 'b' at position 1 replaced by 'x' at position 1.
        QVERIFY(hasRange(res.leftRanges[0],  1, 1));
        QVERIFY(hasRange(res.rightRanges[0], 1, 1));
    }

    // Tail insert: "abc" → "abcXY".
    // The engine emits two separate Insert hunks for X and Y,
    // so we verify total right coverage rather than a single merged span.
    void tailInsert_rightHighlighted() {
        const QStringList left{"abc"};
        const QStringList right{"abcXY"};
        auto res = build(left, right);

        QVERIFY(res.leftRanges[0].isEmpty());  // nothing deleted from left
        // Total coverage on right must be exactly 2 chars ("XY").
        int covered = 0;
        for (const auto& r : res.rightRanges[0]) covered += r.length;
        QCOMPARE(covered, 2);
    }

    // Replace single line: "foo bar" → "foo BAR".
    // Single-line comparison guarantees the outer diff is Replace{0..1,0..1}.
    void replaceSingleLine_bothSidesHighlighted() {
        const QStringList left{"foo bar"};
        const QStringList right{"foo BAR"};
        auto res = build(left, right);

        QCOMPARE(res.leftRanges.size(), 1);
        QCOMPARE(res.rightRanges.size(), 1);
        // "bar" side: some chars changed on left.
        QVERIFY(!res.leftRanges[0].isEmpty());
        // Corresponding changed chars highlighted on right too.
        QVERIFY(!res.rightRanges[0].isEmpty());
        // "foo " (4 chars) must NOT be highlighted on either side.
        for (const auto& r : res.leftRanges[0])
            QVERIFY(r.start >= 4);
        for (const auto& r : res.rightRanges[0])
            QVERIFY(r.start >= 4);
    }

    // Replace unequal height: left has 2 lines, right has 3.
    // Extra right line should be fully highlighted.
    void replaceUnequalHeight_extraLinesFullyHighlighted() {
        const QStringList left{"a", "unchanged", "b"};
        const QStringList right{"x", "unchanged", "y", "extra"};
        // Engine should produce: Replace left[0..2)→right[0..4) (a,b → x,y,extra)
        // or similar. The key is the extra line on right is fully highlighted.
        auto res = build(left, right);

        // Find the right doc line for "extra" and check it is fully highlighted.
        // "extra" is right[3], right.size()==4.
        QCOMPARE(res.rightRanges.size(), 4);
        // right[3] = "extra" (len 5) — should be fully marked.
        const auto& extraRanges = res.rightRanges[3];
        QVERIFY(!extraRanges.isEmpty());
        int covered = 0;
        for (const auto& r : extraRanges) covered += r.length;
        QCOMPARE(covered, 5);  // entire "extra" (5 chars) covered
    }

    // Completely different strings: entire lines highlighted.
    void completelyDifferentLines_entireLinesHighlighted() {
        const QStringList left{"aaaa"};
        const QStringList right{"bbbb"};
        auto res = build(left, right);
        int leftCovered = 0;
        for (const auto& r : res.leftRanges[0]) leftCovered += r.length;
        QCOMPARE(leftCovered, 4);
        int rightCovered = 0;
        for (const auto& r : res.rightRanges[0]) rightCovered += r.length;
        QCOMPARE(rightCovered, 4);
    }

    // Empty line paired with non-empty: non-empty side fully highlighted.
    void emptyVsNonEmpty() {
        const QStringList left{""};
        const QStringList right{"hello"};
        auto res = build(left, right);
        QVERIFY(res.leftRanges[0].isEmpty());   // nothing to highlight on empty line
        int rightCovered = 0;
        for (const auto& r : res.rightRanges[0]) rightCovered += r.length;
        QCOMPARE(rightCovered, 5);
    }
};

QTEST_MAIN(TestIntraLineDiff)
#include "test_intra_line_diff.moc"
