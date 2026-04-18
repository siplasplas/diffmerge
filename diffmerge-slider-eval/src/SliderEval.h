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
// Returns the start of the nearest matching hunk, or -1 if none found.
int findDiffmergePos(const diffcore::DiffResult& result,
                     const SliderCase& sc);

// Accumulated evaluation statistics.
struct EvalStats {
    int total        = 0;   // total sliders evaluated
    int gnuWrong     = 0;   // delta != 0  (git diff disagrees with human)
    int dmWrong      = 0;   // dm_error != 0  (diffmerge disagrees with human)
    int notFound     = 0;   // slider hunk not found in diffmerge output
    QMap<int, int>  errorHist;  // (dm_pos - human_pos) → count
};

// Pretty-print stats to a string.
QString formatStats(const EvalStats& s);

}  // namespace diffmerge::slidereval
