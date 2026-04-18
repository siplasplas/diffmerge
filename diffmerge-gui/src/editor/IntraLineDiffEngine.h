// IntraLineDiffEngine computes character-level diffs for Replace hunks.
//
// For each line pair in a Replace hunk, runs the O(NP) diff engine on
// individual characters and collects changed character ranges.  Lines that
// are not part of a Replace hunk keep empty range lists (no char highlight).
//
// Unequal-height Replace hunks: lines paired index-by-index up to
// min(leftCount, rightCount); extra lines on the longer side are fully
// highlighted (the entire line is considered changed).

#ifndef DIFFMERGE_GUI_INTRALINEDIFFENGINE_H
#define DIFFMERGE_GUI_INTRALINEDIFFENGINE_H

#include <QVector>
#include <QStringList>

#include <diffcore/DiffTypes.h>

namespace diffmerge::gui {

class IntraLineDiffEngine {
public:
    struct CharRange {
        int start  = 0;
        int length = 0;
    };

    struct Result {
        // One entry per left/right doc line.  Empty vector = no char highlight.
        QVector<QVector<CharRange>> leftRanges;
        QVector<QVector<CharRange>> rightRanges;
    };

    // Computes char-level diff ranges for every Replace hunk in `diff`.
    // leftLines / rightLines are the original file contents.
    static Result compute(const diffcore::DiffResult& diff,
                          const QStringList& leftLines,
                          const QStringList& rightLines);

private:
    struct LinePairResult {
        QVector<CharRange> leftRanges;
        QVector<CharRange> rightRanges;
    };
    static LinePairResult diffLinePair(const QString& left, const QString& right);
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_INTRALINEDIFFENGINE_H
