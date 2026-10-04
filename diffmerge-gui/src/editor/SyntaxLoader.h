#pragma once
#include <memory>
#include <QString>
#include <qce/IHighlighter.h>

namespace diffmerge::gui {
// GUI-thread only; reads local Kate data without downloading or writing it.
std::unique_ptr<qce::IHighlighter> loadSyntax(const QString& fileName, bool dark, QString& language);
}
