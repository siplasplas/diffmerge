#pragma once
#include <memory>
#include <QString>
#include <qce/IHighlighter.h>

namespace diffmerge::gui {
// GUI-thread only; reads local Kate data without downloading or writing it.
// Loaded definitions are shared: parsing one (Markdown includes dozens of
// languages) costs far more than the comparison itself. A cached definition
// is reused while its XML file is unchanged; clearSyntaxCache() drops all of
// them, e.g. after the Kate data was updated.
std::shared_ptr<const qce::IHighlighter> loadSyntax(const QString& fileName, bool dark, QString& language);
void clearSyntaxCache();
}
