#include "ColorScheme.h"

#include <QApplication>
#include <QPalette>

namespace diffmerge::gui {

QColor ColorScheme::backgroundFor(diffcore::ChangeType ct) const {
    switch (ct) {
        case diffcore::ChangeType::Insert:  return insertBg;
        case diffcore::ChangeType::Delete:  return deleteBg;
        case diffcore::ChangeType::Replace: return replaceBg;
        case diffcore::ChangeType::Equal:   return QColor();  // Invalid = no fill
    }
    return QColor();
}

QColor ColorScheme::stripeFor(diffcore::ChangeType ct) const {
    switch (ct) {
        case diffcore::ChangeType::Insert:  return sideMarginInsertStripe;
        case diffcore::ChangeType::Delete:  return sideMarginDeleteStripe;
        case diffcore::ChangeType::Replace: return sideMarginReplaceStripe;
        case diffcore::ChangeType::Equal:   return QColor();
    }
    return QColor();
}

ColorScheme ColorScheme::lightDefault() {
    ColorScheme s;
    s.darkTheme = false;

    // Hunk backgrounds - subtle tints, readable on white.
    s.insertBg       = QColor(220, 245, 220);   // Light green
    s.deleteBg       = QColor(250, 220, 220);   // Light red
    s.replaceBg      = QColor(220, 230, 250);   // Light blue
    s.replaceCharBg  = QColor(130, 170, 255);   // Stronger blue for changed chars
    s.placeholderBg  = QColor(245, 245, 240);
    s.placeholderFg = QColor(180, 180, 175);

    // Gutter.
    s.gutterBg  = QColor(240, 240, 235);
    s.gutterFg  = QColor(120, 120, 115);
    s.gutterSeparator = QColor(200, 200, 195);

    // Side margin.
    s.sideMarginBg = QColor(248, 248, 245);
    s.sideMarginInsertStripe  = QColor(100, 180, 100);
    s.sideMarginDeleteStripe  = QColor(200, 100, 100);
    s.sideMarginReplaceStripe = QColor(100, 140, 200);

    return s;
}

ColorScheme ColorScheme::darkDefault() {
    ColorScheme s;
    s.darkTheme = true;

    // Hunk backgrounds - muted, readable on dark gray.
    s.insertBg      = QColor(40, 80, 40);
    s.deleteBg      = QColor(90, 40, 40);
    s.replaceBg     = QColor(40, 60, 90);
    s.replaceCharBg = QColor(50, 90, 180);   // Stronger blue for changed chars
    s.placeholderBg = QColor(50, 50, 48);
    s.placeholderFg = QColor(90, 90, 85);

    s.gutterBg  = QColor(55, 55, 52);
    s.gutterFg  = QColor(150, 150, 145);
    s.gutterSeparator = QColor(80, 80, 75);

    s.sideMarginBg = QColor(45, 45, 42);
    s.sideMarginInsertStripe  = QColor(100, 170, 100);
    s.sideMarginDeleteStripe  = QColor(190, 100, 100);
    s.sideMarginReplaceStripe = QColor(110, 150, 210);

    return s;
}

ColorScheme ColorScheme::forSystem() {
    // Detect dark mode via window background lightness.
    if (qApp) {
        const QColor windowBg = qApp->palette().color(QPalette::Window);
        if (windowBg.lightness() < 128) {
            return darkDefault();
        }
    }
    return lightDefault();
}

}  // namespace diffmerge::gui
