// Unit tests for DiffEngine - covers basic cases, edge cases, options,
// and the merge-to-Replace behavior.

#include <QObject>
#include <QStringList>
#include <QTest>

#include "diffcore/DiffEngine.h"
#include "diffcore/SequenceDiff.h"

#include <span>
#include <string>

using namespace diffcore;

class TestEngine : public QObject {
    Q_OBJECT

private slots:
    void spacingAlignmentPreservesInsertedLineAndFormatting() {
        DiffOptions opts;
        opts.alignWhitespaceChanges = true;
        DiffEngine engine;
        const QStringList left{"header", "  a:=2;", "  b:=3;", "footer"};
        const QStringList right{"header", "new();", "    a := 2;", "    b := 3;  ", "footer"};
        const auto result = engine.compute(left, right, opts);
        QCOMPARE(result.hunks.size(), size_t(4));
        QCOMPARE(result.hunks[1].type, ChangeType::Insert);
        QCOMPARE(result.hunks[1].rightRange.start, 1);
        QCOMPARE(result.hunks[1].rightRange.count, 1);
        QCOMPARE(result.hunks[2].type, ChangeType::Replace);
        QCOMPARE(result.hunks[2].leftRange.start, 1);
        QCOMPARE(result.hunks[2].rightRange.start, 2);
        QCOMPARE(result.hunks[2].leftRange.count, 2);
        QCOMPARE(result.hunks[2].rightRange.count, 2);
        QCOMPARE(result.stats.editDistance, 5);
        const auto reverse = engine.compute(right, left, opts);
        QCOMPARE(reverse.hunks[1].type, ChangeType::Delete);
        QCOMPARE(reverse.hunks[2].leftRange.start, 2);
        QCOMPARE(reverse.hunks[2].rightRange.start, 1);
    }

    void exactAndTrimmedAnchorsOutrankSpacingFreeMatches() {
        DiffOptions opts;
        opts.alignWhitespaceChanges = true;
        DiffEngine engine;
        auto result = engine.compute({"a b", "ab"}, {"ab"}, opts);
        QCOMPARE(result.hunks.size(), size_t(2));
        QCOMPARE(result.hunks[0].type, ChangeType::Delete);
        QCOMPARE(result.hunks[1].type, ChangeType::Equal);
        QCOMPARE(result.hunks[1].leftRange.start, 1);
        result = engine.compute({"a b", " ab "}, {"    ab"}, opts);
        QCOMPARE(result.hunks.size(), size_t(2));
        QCOMPARE(result.hunks[0].type, ChangeType::Delete);
        QCOMPARE(result.hunks[1].type, ChangeType::Replace);
        QCOMPARE(result.hunks[1].leftRange.start, 1);
    }

    void spacingAlignmentDoesNotHideLiteralOrInteriorSpaces() {
        DiffOptions opts;
        opts.alignWhitespaceChanges = true;
        DiffEngine engine;
        for (const auto& pair : std::vector<std::pair<QString, QString>>{
                 {"a b", "a  b"}, {"\"a b\"", "\"ab\""},
                 {"'a b'", "'ab'"}, {"  x", "x"}, {"x  ", "x"}}) {
            const auto result = engine.compute({pair.first}, {pair.second}, opts);
            QVERIFY(!result.isIdentical());
            QCOMPARE(result.hunks[0].type, ChangeType::Replace);
            QCOMPARE(result.stats.editDistance, 2);
        }
        opts.ignoreTrailingWhitespace = true;
        QVERIFY(engine.compute({"x  "}, {"x"}, opts).isIdentical());
        QVERIFY(!engine.compute({"a b"}, {"a  b"}, opts).isIdentical());
        opts.mergeReplaceHunks = false;
        const auto unmerged = engine.compute({"a:=2;"}, {"a := 2;"}, opts);
        QCOMPARE(unmerged.hunks.size(), size_t(2));
        QCOMPARE(unmerged.hunks[0].type, ChangeType::Delete);
        QCOMPARE(unmerged.hunks[1].type, ChangeType::Insert);
    }

    void spacingAlignmentHasOrderedCompleteRanges() {
        // Exhaust short sequences with duplicate/ambiguous spacing keys.
        const QStringList alphabet{"x", " x ", "x x", "xx"};
        std::vector<QStringList> sequences{{}};
        for (const auto& a : alphabet) {
            sequences.push_back({a});
            for (const auto& b : alphabet) sequences.push_back({a, b});
        }
        DiffOptions opts;
        opts.alignWhitespaceChanges = true;
        DiffEngine engine;
        for (bool merge : {false, true}) {
            opts.mergeReplaceHunks = merge;
            for (const auto& left : sequences) for (const auto& right : sequences) {
                const auto result = engine.compute(left, right, opts);
                int l = 0, r = 0, cost = 0;
                for (const auto& h : result.hunks) {
                    QCOMPARE(h.leftRange.start, l);
                    QCOMPARE(h.rightRange.start, r);
                    QVERIFY(h.leftRange.count || h.rightRange.count);
                    if (h.type == ChangeType::Equal) {
                        QCOMPARE(h.leftRange.count, h.rightRange.count);
                        for (int i = 0; i < h.leftRange.count; ++i)
                            QCOMPARE(left[l + i], right[r + i]);
                    } else cost += h.leftRange.count + h.rightRange.count;
                    l = h.leftRange.end();
                    r = h.rightRange.end();
                }
                QCOMPARE(l, left.size());
                QCOMPARE(r, right.size());
                QCOMPARE(result.stats.editDistance, cost);
            }
        }
    }

    void sequenceSupportsCharactersTokensAndViews() {
        auto chars = SequenceDiff::compute(std::string("abc"), std::string("axc"));
        QCOMPARE(chars.editDistance, 2);
        QCOMPARE(chars.hunks.size(), size_t(3));
        QCOMPARE(chars.hunks[1].type, ChangeType::Replace);
        QCOMPARE(chars.hunks[1].leftRange.start, 1);
        QCOMPARE(chars.hunks[1].rightRange.count, 1);

        struct Token {
            std::string text;
            bool operator==(const Token&) const = default;
        };
        const std::vector<Token> left{{"return"}, {"oldName"}, {";"}};
        const std::vector<Token> right{{"return"}, {"newName"}, {";"}, {"extra"}};
        const auto tokens = SequenceDiff::compute(std::span<const Token>(left),
                                                   std::span<const Token>(right));
        QCOMPARE(tokens.leftSize, 3);
        QCOMPARE(tokens.rightSize, 4);
        QCOMPARE(tokens.editDistance, 3);
        QCOMPARE(tokens.hunks.front().type, ChangeType::Equal);
        QCOMPARE(tokens.hunks.back().type, ChangeType::Insert);
        QCOMPARE(tokens.hunks.back().rightRange.start, 3);

        const auto unicode = SequenceDiff::compute(QStringLiteral("zażółć"),
                                                      QStringLiteral("zażółĆ"));
        QCOMPARE(unicode.editDistance, 2);
        QCOMPARE(unicode.hunks.back().leftRange.start, 5);
        const auto raw = SequenceDiff::compute(std::string("a"), std::string("b"), {false, false});
        QCOMPARE(raw.editDistance, 2);
        for (const auto& h : raw.hunks) QVERIFY(h.type != ChangeType::Replace);
    }

    void sequenceScriptsReconstructInputsAndHaveMinimalDistance() {
        // Exhaustive short inputs exercise repeats, swaps, empty inputs and all
        // merge options. A separate dynamic-programming oracle checks distance.
        std::vector<std::vector<int>> sequences;
        for (int length = 0; length <= 4; ++length) {
            for (int bits = 0; bits < (1 << length); ++bits) {
                std::vector<int> seq;
                for (int i = 0; i < length; ++i) seq.push_back((bits >> i) & 1);
                sequences.push_back(std::move(seq));
            }
        }
        for (const auto& left : sequences) {
            for (const auto& right : sequences) {
                const int m = static_cast<int>(left.size());
                const int n = static_cast<int>(right.size());
                std::vector<std::vector<int>> distance(m + 1, std::vector<int>(n + 1));
                for (int i = 0; i <= m; ++i) distance[i][0] = i;
                for (int j = 0; j <= n; ++j) distance[0][j] = j;
                for (int i = 1; i <= m; ++i)
                    for (int j = 1; j <= n; ++j)
                        distance[i][j] = left[i - 1] == right[j - 1] ? distance[i - 1][j - 1]
                            : 1 + std::min(distance[i - 1][j], distance[i][j - 1]);
                for (bool merge : {false, true}) {
                    for (bool coalesce : {false, true}) {
                        const auto result = SequenceDiff::compute(left, right, {merge, coalesce});
                        QCOMPARE(result.editDistance, distance[m][n]);
                        QCOMPARE(result.isIdentical(), left == right);
                        int l = 0, r = 0, edits = 0;
                        std::vector<int> reconstructed;
                        for (const auto& h : result.hunks) {
                            QCOMPARE(h.leftRange.start, l);
                            QCOMPARE(h.rightRange.start, r);
                            QVERIFY(h.leftRange.count >= 0 && h.leftRange.end() <= m);
                            QVERIFY(h.rightRange.count >= 0 && h.rightRange.end() <= n);
                            if (h.type == ChangeType::Equal) {
                                QCOMPARE(h.leftRange.count, h.rightRange.count);
                                for (int i = 0; i < h.leftRange.count; ++i) {
                                    QCOMPARE(left[l + i], right[r + i]);
                                    reconstructed.push_back(left[l + i]);
                                }
                            } else {
                                edits += h.leftRange.count + h.rightRange.count;
                                if (h.type == ChangeType::Insert) QCOMPARE(h.leftRange.count, 0);
                                if (h.type == ChangeType::Delete) QCOMPARE(h.rightRange.count, 0);
                                reconstructed.insert(reconstructed.end(),
                                    right.begin() + r, right.begin() + h.rightRange.end());
                            }
                            l = h.leftRange.end();
                            r = h.rightRange.end();
                        }
                        QCOMPARE(l, m);
                        QCOMPARE(r, n);
                        QCOMPARE(edits, result.editDistance);
                        QVERIFY(reconstructed == right);
                    }
                }
            }
        }
    }

    void sequenceRejectsUnrepresentableSize() {
        struct OversizedSequence {
            using value_type = int;
            size_t size() const { return size_t(std::numeric_limits<int>::max()) + 1; }
            value_type operator[](int) const { return 0; }
        };
        const OversizedSequence sequence;
        QVERIFY_EXCEPTION_THROWN(SequenceDiff::compute(sequence, sequence), std::length_error);
    }

    // --- Trivial cases ---

    void identicalFiles() {
        QStringList lines{"alpha", "beta", "gamma"};
        DiffEngine eng;
        DiffResult r = eng.compute(lines, lines);
        QVERIFY(r.isIdentical());
        QCOMPARE(r.stats.additions, 0);
        QCOMPARE(r.stats.deletions, 0);
        QCOMPARE(r.stats.modifications, 0);
    }

    void bothEmpty() {
        DiffEngine eng;
        DiffResult r = eng.compute({}, {});
        QVERIFY(r.hunks.empty());
        QCOMPARE(r.leftLineCount, 0);
        QCOMPARE(r.rightLineCount, 0);
    }

    void leftEmpty() {
        QStringList right{"a", "b", "c"};
        DiffEngine eng;
        DiffResult r = eng.compute({}, right);
        QCOMPARE(int(r.hunks.size()), 1);
        QCOMPARE(int(r.hunks[0].type), int(ChangeType::Insert));
        QCOMPARE(r.hunks[0].rightRange.count, 3);
        QCOMPARE(r.stats.additions, 3);
    }

    void rightEmpty() {
        QStringList left{"a", "b"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, {});
        QCOMPARE(int(r.hunks.size()), 1);
        QCOMPARE(int(r.hunks[0].type), int(ChangeType::Delete));
        QCOMPARE(r.hunks[0].leftRange.count, 2);
        QCOMPARE(r.stats.deletions, 2);
    }

    // --- Basic operations ---

    void singleInsertion() {
        QStringList left{"a", "c"};
        QStringList right{"a", "b", "c"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.additions, 1);
        QCOMPARE(r.stats.deletions, 0);
        QCOMPARE(r.stats.modifications, 0);
    }

    void singleDeletion() {
        QStringList left{"a", "b", "c"};
        QStringList right{"a", "c"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.additions, 0);
        QCOMPARE(r.stats.deletions, 1);
    }

    void singleReplace() {
        // A change in one line should become one Replace hunk.
        QStringList left{"a", "b", "c"};
        QStringList right{"a", "B", "c"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.modifications, 1);
        QCOMPARE(r.stats.additions, 1);
        QCOMPARE(r.stats.deletions, 1);

        // Find the replace hunk and verify its ranges.
        bool found = false;
        for (const Hunk& h : r.hunks) {
            if (h.type == ChangeType::Replace) {
                QCOMPARE(h.leftRange.start, 1);
                QCOMPARE(h.leftRange.count, 1);
                QCOMPARE(h.rightRange.start, 1);
                QCOMPARE(h.rightRange.count, 1);
                found = true;
            }
        }
        QVERIFY(found);
    }

    void mergeDisabledKeepsDeleteAndInsert() {
        QStringList left{"a", "b", "c"};
        QStringList right{"a", "B", "c"};
        DiffOptions opts;
        opts.mergeReplaceHunks = false;
        DiffEngine eng;
        DiffResult r = eng.compute(left, right, opts);

        // With merging off, we expect both Delete and Insert, no Replace.
        int deletes = 0, inserts = 0, replaces = 0;
        for (const Hunk& h : r.hunks) {
            if (h.type == ChangeType::Delete) ++deletes;
            if (h.type == ChangeType::Insert) ++inserts;
            if (h.type == ChangeType::Replace) ++replaces;
        }
        QCOMPARE(replaces, 0);
        QCOMPARE(deletes, 1);
        QCOMPARE(inserts, 1);
    }

    // --- Normalization options ---

    void ignoreCaseMakesEqual() {
        QStringList left{"Hello", "World"};
        QStringList right{"hello", "WORLD"};
        DiffOptions opts;
        opts.ignoreCase = true;
        DiffEngine eng;
        DiffResult r = eng.compute(left, right, opts);
        QVERIFY(r.isIdentical());
    }

    void ignoreWhitespaceCollapsesRuns() {
        QStringList left{"foo  bar", "baz"};
        QStringList right{"foo bar", "baz"};
        DiffOptions opts;
        opts.ignoreWhitespace = true;
        DiffEngine eng;
        DiffResult r = eng.compute(left, right, opts);
        QVERIFY(r.isIdentical());
    }

    void ignoreTrailingWhitespace() {
        QStringList left{"hello   ", "world"};
        QStringList right{"hello", "world"};
        DiffOptions opts;
        opts.ignoreTrailingWhitespace = true;
        DiffEngine eng;
        DiffResult r = eng.compute(left, right, opts);
        QVERIFY(r.isIdentical());
    }

    void caseSensitiveByDefault() {
        QStringList left{"Hello"};
        QStringList right{"hello"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QVERIFY(!r.isIdentical());
    }

    // --- Swap coverage ---

    void leftLongerThanRight() {
        // Internally the engine will swap. Verify left/right stay correct
        // in the output.
        QStringList left{"a", "b", "c", "d", "e", "f"};
        QStringList right{"a", "c", "f"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.deletions, 3);
        QCOMPARE(r.stats.additions, 0);
        // All hunks describing deletes must have right-range count 0
        // and non-empty left-range count.
        for (const Hunk& h : r.hunks) {
            if (h.type == ChangeType::Delete) {
                QVERIFY(h.leftRange.count > 0);
                QCOMPARE(h.rightRange.count, 0);
            }
        }
    }

    void rightLongerThanLeft() {
        QStringList left{"a", "c", "f"};
        QStringList right{"a", "b", "c", "d", "e", "f"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.additions, 3);
        QCOMPARE(r.stats.deletions, 0);
        for (const Hunk& h : r.hunks) {
            if (h.type == ChangeType::Insert) {
                QCOMPARE(h.leftRange.count, 0);
                QVERIFY(h.rightRange.count > 0);
            }
        }
    }

    // --- Sanity: line counts and edit distance ---

    void lineCountsReflectInput() {
        QStringList left{"1", "2", "3", "4", "5"};
        QStringList right{"1", "2"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.leftLineCount, 5);
        QCOMPARE(r.rightLineCount, 2);
    }

    void editDistanceIsExposed() {
        // One changed line in the middle: edit distance should be 2
        // (one delete + one insert) at line level.
        QStringList left{"a", "b", "c"};
        QStringList right{"a", "B", "c"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);
        QCOMPARE(r.stats.editDistance, 2);
    }

    // --- Coalescing adjacent same-type hunks ---

    // Char-by-char input "abc"→"abcXY": O(NP) may emit two Insert{1} blocks.
    // Default coalescing must merge them into one Insert{2}.
    void coalesceAdjacentInserts_producesOneInsertHunk() {
        QStringList left{"a","b","c"};
        QStringList right{"a","b","c","X","Y"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);

        int insertCount = 0, insertTotal = 0;
        for (const auto& h : r.hunks) {
            if (h.type == ChangeType::Insert) {
                ++insertCount;
                insertTotal += h.rightRange.count;
            }
        }
        QCOMPARE(insertCount, 1);
        QCOMPARE(insertTotal, 2);
    }

    // Same input with coalescing disabled: total coverage still correct,
    // but we may get more than one Insert hunk.
    void coalesceDisabled_totalCoveragePreserved() {
        QStringList left{"a","b","c"};
        QStringList right{"a","b","c","X","Y"};
        DiffOptions opts;
        opts.coalesceAdjacentSameType = false;
        DiffEngine eng;
        DiffResult r = eng.compute(left, right, opts);

        int insertTotal = 0;
        for (const auto& h : r.hunks)
            if (h.type == ChangeType::Insert)
                insertTotal += h.rightRange.count;
        QCOMPARE(insertTotal, 2);
    }

    // Adjacent deletes at char level: "XYabc"→"abc" should be one Delete{2}.
    void coalesceAdjacentDeletes_producesOneDeleteHunk() {
        QStringList left{"X","Y","a","b","c"};
        QStringList right{"a","b","c"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);

        int deleteCount = 0, deleteTotal = 0;
        for (const auto& h : r.hunks) {
            if (h.type == ChangeType::Delete) {
                ++deleteCount;
                deleteTotal += h.leftRange.count;
            }
        }
        QCOMPARE(deleteCount, 1);
        QCOMPARE(deleteTotal, 2);
    }

    // With default options, no two adjacent hunks in the output share the same type.
    void coalesceDefault_noAdjacentSameTypeHunks() {
        QStringList left{"a","b","c","d","e"};
        QStringList right{"a","X","Y","d","Z"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);

        for (size_t i = 1; i < r.hunks.size(); ++i)
            QVERIFY(r.hunks[i].type != r.hunks[i-1].type);
    }

    // --- Hunk coverage (hunks span whole file) ---

    void hunksCoverEntireLeftFile() {
        QStringList left{"a", "b", "c", "d", "e"};
        QStringList right{"a", "B", "d", "e", "F"};
        DiffEngine eng;
        DiffResult r = eng.compute(left, right);

        int coveredLeft = 0;
        for (const Hunk& h : r.hunks) {
            coveredLeft += h.leftRange.count;
        }
        QCOMPARE(coveredLeft, r.leftLineCount);

        int coveredRight = 0;
        for (const Hunk& h : r.hunks) {
            coveredRight += h.rightRange.count;
        }
        QCOMPARE(coveredRight, r.rightLineCount);
    }
};

QTEST_APPLESS_MAIN(TestEngine)
#include "test_engine.moc"
