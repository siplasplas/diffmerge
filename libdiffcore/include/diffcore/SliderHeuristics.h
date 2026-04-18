// Slider-placement heuristics: shift Insert/Delete hunks so their boundaries
// match typical human diff preferences. See SliderHeuristics.cpp for the
// individual rules (H1..H5).

#ifndef DIFFCORE_SLIDERHEURISTICS_H
#define DIFFCORE_SLIDERHEURISTICS_H

#include <QStringList>

#include "DiffTypes.h"

namespace diffcore {

// Adjust the start position of each Insert/Delete hunk in `hunks` using
// the slider heuristics, taking the original left/right line sequences
// as context. Replace/Equal hunks are left untouched.
void applySliderHeuristics(std::vector<Hunk>& hunks,
                            const QStringList& left,
                            const QStringList& right);

}  // namespace diffcore

#endif  // DIFFCORE_SLIDERHEURISTICS_H
