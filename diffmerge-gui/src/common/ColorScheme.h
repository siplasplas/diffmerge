// Color scheme for diff visualization. Detects light/dark mode from the
// system palette and picks appropriate background colors for hunks.
// Users can override specific colors; defaults adapt automatically.

#ifndef DIFFMERGE_GUI_COLORSCHEME_H
#define DIFFMERGE_GUI_COLORSCHEME_H

#include <QColor>

#include <diffcore/DiffTypes.h>

namespace diffmerge::gui {

struct ColorScheme {
    QColor insertBg;
    QColor deleteBg;
    QColor replaceBg;
    QColor placeholderBg;  // Empty row background (subtle)
    QColor placeholderFg;  // Color for the "—" glyph

    QColor gutterBg;
    QColor gutterFg;
    QColor gutterSeparator;

    QColor sideMarginBg;
    QColor sideMarginInsertStripe;
    QColor sideMarginDeleteStripe;
    QColor sideMarginReplaceStripe;

    bool darkTheme = false;

    // Returns the background color for a given change type, or an invalid
    // QColor for Equal (no fill).
    QColor backgroundFor(diffcore::ChangeType ct) const;

    // Returns the side-margin stripe color for a given change type, or an
    // invalid QColor for Equal (no stripe).
    QColor stripeFor(diffcore::ChangeType ct) const;

    // Factory: build a default scheme for the current system palette.
    static ColorScheme forSystem();
    static ColorScheme lightDefault();
    static ColorScheme darkDefault();
};

}  // namespace diffmerge::gui

#endif  // DIFFMERGE_GUI_COLORSCHEME_H
