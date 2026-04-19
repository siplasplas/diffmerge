// AlignedLineModel translates a DiffResult into two parallel "aligned"
// line sequences, padded with placeholder lines so that corresponding
// hunks occupy the same vertical position in both editors.
//
// Conceptually:
//   Given a DiffResult with hunks, produce for each side:
//     - The aligned text: original lines + placeholders for missing lines
//     - A mapping: aligned row -> original line number (or -1 for placeholder)
//     - A mapping: aligned row -> ChangeType (for background highlight)
//
// Example:
//   Left file:  ["alpha", "beta", "gamma"]
//   Right file: ["alpha", "BETA", "gamma", "delta"]
//   Hunks: Equal[0..1), Replace[1..2)x[1..2), Equal[2..3)x[2..3), Insert[.)x[3..4)
//
//   Aligned output (4 rows total on both sides):
//     row 0: "alpha"  | "alpha"      (Equal)
//     row 1: "beta"   | "BETA"       (Replace)
//     row 2: "gamma"  | "gamma"      (Equal)
//     row 3: <ph>     | "delta"      (Insert)
//
//   Left row 3 is a placeholder (no text in left file). Gutter shows "—"
//   or nothing; the file-line mapping returns -1.

#ifndef DIFFMERGE_GUI_ALIGNEDLINEMODEL_H
#define DIFFMERGE_GUI_ALIGNEDLINEMODEL_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

#include <diffcore/DiffTypes.h>

namespace diffmerge::gui {

// Which side of the diff a model represents.
enum class Side {
    Left,   // File A
    Right   // File B
};

struct AlignedRow {
    // -1 for placeholder rows, otherwise 0-based line number in the
    // original file. Gutter uses this to decide what number to paint.
    int fileLineNumber = -1;

    // Background fill for the whole row. Equal rows get no fill.
    diffcore::ChangeType changeType = diffcore::ChangeType::Equal;

    // True iff this row is a placeholder (fileLineNumber == -1).
    bool isPlaceholder() const { return fileLineNumber < 0; }
};

class AlignedLineModel {
public:
    // Build the aligned model from a diff result and the original lines
    // from both files. Call once per diff; the result is immutable.
    void build(const diffcore::DiffResult& diff,
               const QStringList& leftLines,
               const QStringList& rightLines);

    // Total number of rows after alignment. Same for both sides.
    int rowCount() const { return static_cast<int>(m_leftRows.size()); }

    // Retrieve metadata for a given aligned row.
    const AlignedRow& row(Side side, int alignedRow) const;

    // Retrieve the display text for a row. Returns an empty string for
    // placeholder rows; the editor widget paints its own placeholder glyph.
    QString text(Side side, int alignedRow) const;

    // Build a single QString with all rows joined by '\n', suitable for
    // QPlainTextEdit::setPlainText(). Placeholder rows produce empty lines.
    QString buildDocumentText(Side side) const;

    // --- qce-specific query methods ---

    // Returns only the real (non-placeholder) lines as document content.
    QStringList documentLines(Side side) const;

    // Describes one run of placeholder rows for qce::FillerState.
    struct FillerInfo {
        int beforeDocLine;  // insert before this 0-based doc line; equals
                            // docLineCount when the filler trails all real lines
        int rowCount;
        diffcore::ChangeType changeType;
    };

    // Returns filler positions; caller maps changeType to color.
    QVector<FillerInfo> fillerRanges(Side side) const;

    // Change type for a real (non-placeholder) doc line (0-based).
    diffcore::ChangeType docLineChangeType(Side side, int docLine) const;

    // Aligned row index where each non-Equal hunk starts. Used for navigation.
    const QVector<int>& hunkAlignedStarts() const { return m_hunkAlignedStarts; }

    // Number of non-placeholder rows strictly before alignedRow on given side.
    // This is the scrollbar doc-line value that brings alignedRow into view.
    int docLineBeforeAligned(Side side, int alignedRow) const;

private:
    // Append an Equal range to both sides' aligned rows.
    void appendEqual(const diffcore::Hunk& h,
                     const QStringList& leftLines,
                     const QStringList& rightLines);
    // Append a pure Insert (right only) - left gets placeholders.
    void appendInsert(const diffcore::Hunk& h,
                      const QStringList& rightLines);
    // Append a pure Delete (left only) - right gets placeholders.
    void appendDelete(const diffcore::Hunk& h,
                      const QStringList& leftLines);
    // Append a Replace hunk - both sides may be different sizes, pad the
    // shorter side with placeholders so rows line up.
    void appendReplace(const diffcore::Hunk& h,
                       const QStringList& leftLines,
                       const QStringList& rightLines);

    std::vector<AlignedRow> m_leftRows;
    std::vector<AlignedRow> m_rightRows;
    QStringList m_leftText;   // Per aligned row (empty if placeholder)
    QStringList m_rightText;
    QVector<int> m_hunkAlignedStarts;  // aligned row start of each non-Equal hunk
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_ALIGNEDLINEMODEL_H
