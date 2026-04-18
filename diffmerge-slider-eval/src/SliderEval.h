#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <diffcore/DiffTypes.h>

namespace diffmerge::slidereval {

// One ambiguous diff block from the corpus.
// blockBegin is 1-based (same convention as GNU diff line numbers).
// human_pos = blockBegin + delta.
struct SliderCase {
    char sign       = '+';  // '+' = insertion in right file, '-' = deletion from left
    int  blockBegin = 0;    // where git diff placed the sliding block (1-based)
    int  delta      = 0;    // shift to get from git diff pos to human preference
};

// One digest entry from a CSV file: one file pair + N sliders that reference it.
struct CsvEntry {
    QString             digest;
    QVector<SliderCase> sliders;
};

// Parse one CSV line: "a1b2c3d4e5f6,+ 1999 -1,- 2050 0"
// Returns a CsvEntry with empty digest on parse failure.
CsvEntry parseCsvLine(const QString& line);

// Load all entries from a CSV file. Skips blank or malformed lines.
QVector<CsvEntry> loadCsvFile(const QString& csvPath);

// Given a DiffResult and a SliderCase, find where diffmerge placed the
// sliding block (1-based line number on the relevant side).
// sign='+' → search Insert hunks, return rightRange start+1.
// sign='-' → search Delete hunks, return leftRange start+1.
// maxSlide: maximum distance (in lines) from blockBegin to accept a hunk.
//   Hunks further away are almost certainly a different change in the file,
//   not the slider. Use e.g. 10.  Pass INT_MAX to disable the filter.
// Returns the 1-based position of the best matching hunk,
// or -1 if none found within maxSlide.
int findDiffmergePos(const diffcore::DiffResult& result,
                     const SliderCase& sc,
                     int maxSlide = 10);

// Accumulated evaluation statistics.
struct EvalStats {
    int total        = 0;   // total sliders evaluated
    int notFound     = 0;   // slider hunk not found within maxSlide

    // gnu_error = -delta  (delta = human_pos - gnu_pos)
    // dm_error  = dm_pos - human_pos
    int gnuWrong     = 0;   // |gnu_error| != 0  (delta != 0)
    int dmWrong      = 0;   // |dm_error|  != 0

    // Comparison (for sliders where hunk was found):
    int dmBetter     = 0;   // |dm_error| < |gnu_error|  — diffmerge closer to human
    int dmWorse      = 0;   // |dm_error| > |gnu_error|  — diffmerge further from human
    int dmTie        = 0;   // |dm_error| == |gnu_error| — same distance as git diff

    QMap<int, int>  errorHist;  // (dm_pos - human_pos) → count
};

// Pretty-print stats to a string.
QString formatStats(const EvalStats& s);

}  // namespace diffmerge::slidereval
