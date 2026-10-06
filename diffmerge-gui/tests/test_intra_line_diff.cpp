// Unit tests for IntraLineDiffEngine.

#include <QObject>
#include <QTest>

#include <diffcore/DiffEngine.h>

#include <diffmerge/IntraLineDiffEngine.h>

using namespace diffmerge::gui;
using Range = IntraLineDiffEngine::CharRange;

class TestIntraLineDiff : public QObject {
    Q_OBJECT

private:
    IntraLineDiffEngine::Result build(const QStringList& left,
                                      const QStringList& right,
                                      IntraLineDiffEngine::Detail detail = IntraLineDiffEngine::Detail::Characters) {
        diffcore::DiffEngine engine;
        diffcore::DiffOptions opts;
        opts.alignWhitespaceChanges = true;
        const auto diff = engine.compute(left, right, opts);
        return IntraLineDiffEngine::compute(diff, left, right, nullptr, detail);
    }

    static bool hasRange(const QVector<Range>& ranges, int start, int len) {
        for (const auto& r : ranges)
            if (r.start == start && r.length == len) return true;
        return false;
    }

private slots:
    void wholeWordsKeepCommonLettersHighlighted() {
        const auto characters=build({"prefix alpha_name suffix"},{"prefix alpha_game suffix"});
        QVERIFY(hasRange(characters.leftRanges[0],13,1));
        const auto words=build({"prefix alpha_name suffix"},{"prefix alpha_game suffix"},IntraLineDiffEngine::Detail::WholeWords);
        QCOMPARE(words.leftRanges[0].size(),1); QCOMPARE(words.rightRanges[0].size(),1);
        QVERIFY(hasRange(words.leftRanges[0],7,10)); QVERIFY(hasRange(words.rightRanges[0],7,10));
        const auto split=build({"keep foobar end"},{"keep foo bar end"},IntraLineDiffEngine::Detail::WholeWords);
        QVERIFY(hasRange(split.leftRanges[0],5,6));
        for(int column : {5,6,7,9,10,11}) {
            bool marked=false;
            for(const auto& range:split.rightRanges[0]) marked |= column>=range.start && column<range.start+range.length;
            QVERIFY(marked);
        }
    }
    void wholeWordsProjectUnicodeAcrossBlockLines() {
        const QStringList left{"alpha old_name",QString::fromUtf8("beta café_name")};
        const QStringList right{"alpha new_name",QString::fromUtf8("beta cafè_name")};
        diffcore::DiffResult diff; diff.hunks={{diffcore::ChangeType::Replace,{0,2},{0,2}}};
        const auto words=IntraLineDiffEngine::compute(diff,left,right,nullptr,IntraLineDiffEngine::Detail::WholeWords);
        QVERIFY(hasRange(words.leftRanges[0],6,8)); QVERIFY(hasRange(words.rightRanges[0],6,8));
        QVERIFY(hasRange(words.leftRanges[1],5,10)); QVERIFY(hasRange(words.rightRanges[1],5,10));
        for(int i=0;i<2;++i) { QCOMPARE(words.leftRanges[i].size(),1); QCOMPARE(words.rightRanges[i].size(),1); }
    }

    void insertedAndRemovedWordsDoNotMatchNeighborLetters() {
        const auto removed = QStringLiteral("modyfikacja");
        const auto left = QStringLiteral("Shift = 0 modyfikacja oznacza trafienie w ludzką preferencję.");
        const auto right = QStringLiteral("Shift = 0 oznacza trafienie w ludzką preferencję.");
        const auto result = build({left}, {right});
        const int start = left.indexOf(removed);
        QVector<bool> marked(left.size(), false);
        for (const auto& range : result.leftRanges[0])
            for (int i = range.start; i < range.start + range.length; ++i) marked[i] = true;
        for (int i = start; i < start + removed.size(); ++i) QVERIFY(marked[i]);
        for (int i = 0; i < left.size(); ++i)
            if (i < start - 1 || i > start + removed.size()) QVERIFY(!marked[i]);
        QVERIFY(result.rightRanges[0].isEmpty());
        const auto reverse = build({right}, {left});
        QVERIFY(reverse.leftRanges[0].isEmpty());
        QCOMPARE(reverse.rightRanges[0].size(), result.leftRanges[0].size());
        for (int i = 0; i < result.leftRanges[0].size(); ++i) {
            QCOMPARE(reverse.rightRanges[0][i].start, result.leftRanges[0][i].start);
            QCOMPARE(reverse.rightRanges[0][i].length, result.leftRanges[0][i].length);
        }
    }

    void wordMatchingSpansLinesAndProjectsOriginalColumns() {
        const QStringList left{"alpha removedword", "beta old_name = 12;"};
        const QStringList right{"alpha", "beta new_name = 13;"};
        diffcore::DiffResult diff;
        diff.hunks = {{diffcore::ChangeType::Replace, {0, 2}, {0, 2}}};
        const auto result = IntraLineDiffEngine::compute(diff, left, right);
        int removedLetters = 0;
        for (const auto& range : result.leftRanges[0]) {
            QVERIFY(range.start >= 5);
            removedLetters += range.length;
        }
        QVERIFY(removedLetters >= QStringLiteral("removedword").size());
        QVERIFY(result.rightRanges[0].isEmpty());
        QVERIFY(hasRange(result.leftRanges[1], 5, 3));
        QVERIFY(hasRange(result.rightRanges[1], 5, 3));
        QVERIFY(hasRange(result.leftRanges[1], 17, 1));
        QVERIFY(hasRange(result.rightRanges[1], 17, 1));
        for (const auto& range : result.leftRanges[1])
            QVERIFY(range.start == 5 || range.start == 17);
    }

    void supplementaryCharactersAndCombiningMarksKeepUtf16Offsets() {
        const auto left = QString::fromUtf8("😀 café_name");
        const auto right = QString::fromUtf8("😀 cafè_name");
        const auto result = build({left}, {right});
        QVERIFY(hasRange(result.leftRanges[0], 7, 1));
        QVERIFY(hasRange(result.rightRanges[0], 7, 1));
        QCOMPARE(result.leftRanges[0].size(), 1);
        QCOMPARE(result.rightRanges[0].size(), 1);
    }

    void reformattedLinesAfterInsertionKeepTheirOwnCharacterChanges() {
        const auto result = build({"a:=2;", "\"a b\""},
                                  {"inserted();", "a := 2;", "\"ab\""});
        QVERIFY(result.leftRanges[0].isEmpty());
        QVERIFY(hasRange(result.rightRanges[1], 1, 1));
        QVERIFY(hasRange(result.rightRanges[1], 4, 1));
        QVERIFY(hasRange(result.leftRanges[1], 2, 1));
        QVERIFY(result.rightRanges[2].isEmpty());
    }

    void whitespaceChangesKeepExactCharacterPositions() {
        // Empty-line slider heuristics must never relocate character changes.
        auto res = build({"a b"}, {"a  b"});
        QVERIFY(res.leftRanges[0].isEmpty());
        int covered = 0;
        for (const auto& r : res.rightRanges[0]) {
            QVERIFY(r.start == 1 || r.start == 2);
            QCOMPARE(r.length, 1);
            covered += r.length;
        }
        QCOMPARE(covered, 1);
    }

    void nonAsciiChangeUsesEditorOffsets() {
        auto res = build({QStringLiteral("zażółć")}, {QStringLiteral("zażółĆ")});
        QVERIFY(hasRange(res.leftRanges[0], 5, 1));
        QVERIFY(hasRange(res.rightRanges[0], 5, 1));
    }

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

QTEST_GUILESS_MAIN(TestIntraLineDiff)
#include "test_intra_line_diff.moc"
