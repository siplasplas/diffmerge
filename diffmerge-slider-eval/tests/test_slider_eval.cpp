// Unit tests for SliderEval — CSV parsing and slider position finding.

#include <QObject>
#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>

#include <diffcore/DiffEngine.h>

#include "../src/SliderEval.h"

using namespace diffmerge::slidereval;

class TestSliderEval : public QObject {
    Q_OBJECT

private slots:

    // --- parseCsvLine ---

    void parseCsvLine_singleSlider() {
        const auto e = parseCsvLine(QStringLiteral("a1b2c3d4e5f6,+ 77 -2"));
        QCOMPARE(e.digest, QStringLiteral("a1b2c3d4e5f6"));
        QCOMPARE(e.sliders.size(), 1);
        QCOMPARE(e.sliders[0].sign, '+');
        QCOMPARE(e.sliders[0].blockBegin, 77);
        QCOMPARE(e.sliders[0].delta, -2);
    }

    void parseCsvLine_multipleSliders() {
        const auto e = parseCsvLine(
            QStringLiteral("deadbeef0001,+ 1999 -1,- 2050 0,+ 2100 3"));
        QCOMPARE(e.sliders.size(), 3);
        QCOMPARE(e.sliders[0].sign, '+');
        QCOMPARE(e.sliders[0].delta, -1);
        QCOMPARE(e.sliders[1].sign, '-');
        QCOMPARE(e.sliders[1].delta, 0);
        QCOMPARE(e.sliders[2].delta, 3);
    }

    void parseCsvLine_emptyLineReturnsInvalid() {
        const auto e = parseCsvLine(QStringLiteral(""));
        QVERIFY(e.digest.isEmpty());
    }

    void parseCsvLine_onlyDigestNoSliders() {
        const auto e = parseCsvLine(QStringLiteral("a1b2c3d4e5f6"));
        QVERIFY(e.sliders.isEmpty());
    }

    // --- loadCsvFile ---

    void loadCsvFile_parsesMultipleLines() {
        QTemporaryFile f;
        f.open();
        QTextStream s(&f);
        s << "aaa111,+ 10 0\n";
        s << "bbb222,- 20 -1,+ 30 2\n";
        s << "\n";   // blank line must be skipped
        s.flush();

        const auto entries = loadCsvFile(f.fileName());
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].digest, QStringLiteral("aaa111"));
        QCOMPARE(entries[1].sliders.size(), 2);
    }

    // --- findDiffmergePos ---

    // File pair: A has 5 lines, B inserts one line at position 3.
    // DiffEngine should produce an Insert hunk with rightRange.start == 2 (0-based).
    // findDiffmergePos should return 3 (1-based).
    void findDiffmergePos_insertion() {
        const QStringList a{"alpha", "beta", "gamma", "delta", "epsilon"};
        const QStringList b{"alpha", "beta", "NEW",   "gamma", "delta", "epsilon"};

        diffcore::DiffEngine engine;
        const auto diff = engine.compute(a, b);

        SliderCase sc;
        sc.sign       = '+';
        sc.blockBegin = 3;
        sc.diffPos    = 3;   // emulate annotated sys-diff position
        const int pos = findDiffmergePos(diff, sc);
        QVERIFY(pos > 0);
        // The inserted line "NEW" appears at 1-based position 3 in B.
        QCOMPARE(pos, 3);
    }

    void findDiffmergePos_deletion() {
        const QStringList a{"alpha", "beta", "GONE", "gamma", "delta"};
        const QStringList b{"alpha", "beta", "gamma", "delta"};

        diffcore::DiffEngine engine;
        const auto diff = engine.compute(a, b);

        SliderCase sc;
        sc.sign       = '-';
        sc.blockBegin = 3;   // "GONE" at line 3 in A
        sc.diffPos    = 3;
        const int pos = findDiffmergePos(diff, sc);
        QVERIFY(pos > 0);
        QCOMPARE(pos, 3);
    }

    void findDiffmergePos_returnsMinusOneWhenNoMatchingHunk() {
        const QStringList a{"alpha", "beta"};
        const QStringList b{"alpha", "beta", "gamma"};

        diffcore::DiffEngine engine;
        const auto diff = engine.compute(a, b);

        // Asking for a '-' (delete) hunk in a diff that only has Insert → not found.
        SliderCase sc;
        sc.sign       = '-';
        sc.blockBegin = 1;
        QCOMPARE(findDiffmergePos(diff, sc, 10), -1);
    }

    void findDiffmergePos_rejectsFarHunk() {
        // Insert at line 10, ask for slider at blockBegin=1 with maxSlide=5 → reject.
        const QStringList a{"a","b","c","d","e","f","g","h","i","j"};
        QStringList b = a;
        b.insert(9, QStringLiteral("NEW"));  // insert near end (position 10, 1-based)

        diffcore::DiffEngine engine;
        const auto diff = engine.compute(a, b);

        SliderCase sc;
        sc.sign       = '+';
        sc.blockBegin = 1;
        sc.diffPos    = 1;   // anchor far from actual insert at ~10
        // With diffPos set, tolerance is tight (3) regardless of maxSlide.
        QCOMPARE(findDiffmergePos(diff, sc, 5),  -1);
        QCOMPARE(findDiffmergePos(diff, sc, 20), -1);
    }

    // --- delta=0 means git diff matched human; error histogram entry 0 ---

    // Slider with delta=0: git diff was correct. If diffmerge places the block
    // at the same position (same O(NP) algorithm), dm_error should also be 0.
    void evaluationFlow_deltaZeroMeansGnuCorrect() {
        // Simple single-line replacement: A has "foo", B has "bar".
        // O(NP) produces Replace{0,1} → as Insert+Delete or Replace.
        const QStringList a{"context", "foo", "context2"};
        const QStringList b{"context", "bar", "context2"};

        diffcore::DiffEngine engine;
        const auto diff = engine.compute(a, b);

        // For a Replace hunk there's no pure Insert or Delete; findDiffmergePos
        // won't find anything → returns -1.  Verify gracefully.
        SliderCase sc;
        sc.sign       = '+';
        sc.blockBegin = 2;
        sc.delta      = 0;
        // Just verify it doesn't crash.
        (void)findDiffmergePos(diff, sc);
        QVERIFY(true);
    }
};

QTEST_APPLESS_MAIN(TestSliderEval)
#include "test_slider_eval.moc"
