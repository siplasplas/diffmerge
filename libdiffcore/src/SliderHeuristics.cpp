#include "diffcore/SliderHeuristics.h"

#include <algorithm>
#include <QString>

namespace diffcore {

namespace {

// Leading indentation width in columns. Tab counts as 4 spaces.
int indentWidth(const QString& line) {
    int w = 0;
    for (QChar c : line) {
        if (c == QLatin1Char('\t'))      w += 4;
        else if (c == QLatin1Char(' '))  ++w;
        else break;
    }
    return w;
}

// Returns the 0-based adjusted position, or `p` if no heuristic fires.
// `rel` is the file side the block is visible in (right for Insert,
// left for Delete).
//
// Heuristics tried in order:
//   H5: JSON "},\n{" boundary → slide back by 1.
//   H2: smallest k in slide-left range with indent drop; fallback to
//       max_k when all absorbed lines start with "//".
//   H1: block bounded by empty lines on both sides → slide forward by
//       min(n, m).
//   H3: separator line (/*, ;;***, ;***, ;;;;) before block equals last
//       block line → slide back by 1.
//   H4: "#pragma mark" / "// MARK" absorbed into the block.
int adjustPosition(int p, int size, const QStringList& rel) {
    const int N = rel.size();
    if (p < 0 || p >= N || size < 1) return p;

    // H5: "},\n{" boundary with same two lines at end of block.
    if (p >= 2 && p + size - 1 < N &&
        rel[p - 2].trimmed() == QStringLiteral("},") &&
        rel[p - 1].trimmed() == QStringLiteral("{") &&
        rel[p - 2] == rel[p + size - 2] &&
        rel[p - 1] == rel[p + size - 1]) {
        return p - 1;
    }

    // H2: slide-left range with indent drop.
    int k2 = 0;
    while (p - (k2 + 1) >= 0 && p + size - (k2 + 1) < N &&
           rel[p - (k2 + 1)] == rel[p + size - (k2 + 1)])
        ++k2;
    if (k2 > 0) {
        const int indFirst = indentWidth(rel[p]);
        for (int k = 1; k <= k2; ++k) {
            if (indentWidth(rel[p - k]) < indFirst) return p - k;
        }
        if (indentWidth(rel[p - k2]) == indFirst) {
            bool allComments = true;
            for (int i = 1; i <= k2; ++i) {
                if (!rel[p - i].trimmed().startsWith(QStringLiteral("//"))) {
                    allComments = false;
                    break;
                }
            }
            if (allComments) return p - k2;
        }
    }

    // H1: empty-line runs at both boundaries.
    int n = 0;
    while (n < size && p + n < N && rel[p + n].trimmed().isEmpty()) ++n;
    int m = 0;
    while (p + size + m < N && rel[p + size + m].trimmed().isEmpty()) ++m;
    const int k1 = std::min(n, m);
    if (k1 > 0) return p + k1;

    // H3: visual section separator before block.
    if (p > 0 && p + size - 1 < N &&
        rel[p - 1] == rel[p + size - 1]) {
        const QString t = rel[p - 1].trimmed();
        if (t.startsWith(QStringLiteral("/*"))    ||
            t.startsWith(QStringLiteral(";;***")) ||
            t.startsWith(QStringLiteral(";***"))  ||
            t.startsWith(QStringLiteral(";;;;"))) {
            return p - 1;
        }
    }

    // H4: absorb "#pragma mark" / "// MARK" into the block.
    auto isMark = [](const QString& line) {
        const QString t = line.trimmed();
        return t.startsWith(QStringLiteral("#pragma mark")) ||
               t.startsWith(QStringLiteral("// MARK"));
    };
    int k4 = 0;
    while (p - (k4 + 1) >= 0 && p + size - (k4 + 1) < N &&
           rel[p - (k4 + 1)] == rel[p + size - (k4 + 1)])
        ++k4;
    for (int j = k4; j >= 1; --j) {
        if (isMark(rel[p - j])) return p - j;
    }
    if (p > 0 && isMark(rel[p - 1])) return p - 1;

    return p;
}

}  // namespace

void applySliderHeuristics(std::vector<Hunk>& hunks,
                            const QStringList& left,
                            const QStringList& right) {
    for (Hunk& h : hunks) {
        if (h.type == ChangeType::Insert) {
            const int oldP = h.rightRange.start;
            const int newP = adjustPosition(oldP, h.rightRange.count, right);
            const int delta = newP - oldP;
            h.rightRange.start += delta;
            h.leftRange.start  += delta;
        } else if (h.type == ChangeType::Delete) {
            const int oldP = h.leftRange.start;
            const int newP = adjustPosition(oldP, h.leftRange.count, left);
            const int delta = newP - oldP;
            h.leftRange.start  += delta;
            h.rightRange.start += delta;
        }
    }
}

}  // namespace diffcore
