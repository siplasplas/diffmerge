// Refine Replace hunks using words across the whole block, followed by
// character comparison within corresponding changed words. Words contain
// Unicode letters, numbers, marks and underscores; punctuation and spacing
// remain significant. Results use original document lines and UTF-16 columns.
// Inserted/deleted tokens are fully highlighted. No language-specific lexer
// or whitespace suppression is applied.

#ifndef DIFFMERGE_GUI_INTRALINEDIFFENGINE_H
#define DIFFMERGE_GUI_INTRALINEDIFFENGINE_H

#include <QVector>
#include <QStringList>

#include <diffcore/DiffTypes.h>
#include <diffcore/ComputationControl.h>

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

    // Computes word-refined character ranges for every Replace hunk in `diff`.
    // leftLines / rightLines are the original file contents.
    static Result compute(const diffcore::DiffResult& diff,
                          const QStringList& leftLines,
                          const QStringList& rightLines, diffcore::ComputationControl* control = nullptr);

private:
    struct LinePairResult {
        QVector<CharRange> leftRanges;
        QVector<CharRange> rightRanges;
    };
    static LinePairResult diffText(const QString& left, const QString& right, diffcore::ComputationControl* control);
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_INTRALINEDIFFENGINE_H
