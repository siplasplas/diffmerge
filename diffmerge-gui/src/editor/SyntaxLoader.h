#pragma once
#include <functional>
#include <memory>
#include <QObject>
#include <QString>
#include <qce/IHighlighter.h>

namespace diffmerge::gui {
// GUI-thread only; reads local Kate data without downloading or writing it.
// Loaded definitions are shared: parsing one (Markdown includes dozens of
// languages) costs far more than the comparison itself. A cached definition
// is reused while its XML file is unchanged; clearSyntaxCache() drops all of
// them, e.g. after the Kate data was updated.
std::shared_ptr<const qce::IHighlighter> loadSyntax(const QString& fileName, bool dark, QString& language);

// A null syntax with an empty language: no usable definition.
using SyntaxCallback = std::function<void(std::shared_ptr<const qce::IHighlighter> syntax, const QString& language)>;
// Calls `done` at once when the definition is cached or none matches the file
// name. Otherwise the definition is parsed in a worker thread and `done` runs
// later on the GUI thread, unless `context` has been destroyed by then.
// Simultaneous requests for one definition share a single load.
void loadSyntaxAsync(const QString& fileName, bool dark, QObject* context, SyntaxCallback done);
void clearSyntaxCache();
}
