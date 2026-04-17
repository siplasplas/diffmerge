// High-level diff API. Takes two sequences of lines (QStringList) and
// produces a DiffResult that the GUI/CLI can render directly.
//
// Internally:
//  1. LineInterner assigns integer IDs to unique (normalized) lines.
//  2. The templated O(NP) engine computes an edit script over the IDs.
//  3. Block results are translated into public Hunk objects, with an
//     optional merge step turning adjacent Delete+Insert into Replace.
//  4. If the engine swapped its inputs internally, the adapter swaps
//     coordinates back so the user always sees (left, right) as given.

#ifndef DIFFCORE_DIFFENGINE_H
#define DIFFCORE_DIFFENGINE_H

#include <QStringList>

#include "DiffTypes.h"

namespace diffcore {

class DiffEngine {
public:
    DiffResult compute(const QStringList& left,
                       const QStringList& right,
                       const DiffOptions& opts = {});
};

}  // namespace diffcore

#endif  // DIFFCORE_DIFFENGINE_H
