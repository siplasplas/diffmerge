#include "SyntaxLoader.h"
#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

namespace diffmerge::gui {
namespace {
KateTheme syntaxTheme(bool dark) {
    KateTheme theme;
    theme.name = dark ? QStringLiteral("DiffMerge dark") : QStringLiteral("DiffMerge light");
    const QColor normal = dark ? QColor(220, 220, 220) : QColor(32, 32, 32);
    const QStringList styles{
        "Normal", "Keyword", "ControlFlow", "Function", "Variable", "Operator", "BuiltIn",
        "Extension", "Preprocessor", "Attribute", "Char", "SpecialChar", "String", "VerbatimString",
        "SpecialString", "Import", "DataType", "DecVal", "BaseN", "Float", "Constant", "Comment",
        "Documentation", "Annotation", "CommentVar", "RegionMarker", "Information", "Warning",
        "Alert", "Error", "Others"
    };
    for (const auto& style : styles) theme.styles.insert(style, {normal, {}, false, false, false});
    const auto color = [&](const QStringList& names, QColor foreground, bool bold = false, bool italic = false) {
        for (const auto& name : names) theme.styles[name] = {foreground, {}, bold, italic, false};
    };
    color({"Keyword", "ControlFlow", "Import"}, dark ? QColor(130, 180, 255) : QColor(0, 0, 170), true);
    color({"DataType", "BuiltIn", "Extension", "Attribute"}, dark ? QColor(110, 210, 210) : QColor(0, 110, 110));
    color({"String", "VerbatimString", "SpecialString", "Char", "SpecialChar"}, dark ? QColor(240, 170, 140) : QColor(170, 30, 30));
    color({"DecVal", "BaseN", "Float", "Constant"}, dark ? QColor(210, 180, 255) : QColor(130, 70, 20));
    color({"Comment", "Documentation", "CommentVar", "RegionMarker"}, dark ? QColor(155, 175, 155) : QColor(100, 120, 100), false, true);
    color({"Preprocessor", "Others"}, dark ? QColor(170, 210, 140) : QColor(0, 110, 0));
    color({"Alert", "Error", "Warning"}, dark ? QColor(255, 135, 135) : QColor(200, 0, 0), true);
    return theme;
}

// Keyed by XML path, its size and mtime, and the theme.
QHash<QString, std::shared_ptr<const qce::IHighlighter>>& syntaxCache() {
    static QHash<QString, std::shared_ptr<const qce::IHighlighter>> cache;
    return cache;
}
// Raised by clearSyntaxCache() so that loads started before it are not cached.
int& cacheEpoch() {
    static int epoch = 0;
    return epoch;
}
QString cacheKey(const QString& path, const qce::kate::LanguageEntry& entry, bool dark) {
    return QStringLiteral("%1\n%2\n%3\n%4").arg(path).arg(entry.size).arg(entry.mtime).arg(dark);
}

// One definition to try; `known` marks a cache entry, which needs no loading.
struct Candidate {
    QString key, path, name;
    bool known = false;
    std::shared_ptr<const qce::IHighlighter> syntax;
};
struct Waiting {
    QPointer<QObject> context;
    SyntaxCallback done;
};
// Callers waiting for a load, by the key of its first candidate.
QHash<QString, QList<Waiting>>& waitingLoads() {
    static QHash<QString, QList<Waiting>> waiting;
    return waiting;
}
}

std::shared_ptr<const qce::IHighlighter> loadSyntax(const QString& fileName, bool dark, QString& language) {
    language.clear();
    if (fileName.isEmpty()) return {};
    const auto index = qce::kate::KateSyntaxIndex::load(qce::kate::dataDir());
    for (const auto* entry : index.forFileName(fileName)) {
        const QString path = index.filePath(*entry);
        const QString key = cacheKey(path, *entry, dark);
        auto& cache = syntaxCache();
        auto cached = cache.constFind(key);
        if (cached == cache.constEnd())
            cached = cache.insert(key, std::shared_ptr<const qce::IHighlighter>(KateXmlReader::load(path, syntaxTheme(dark), index)));
        if (*cached) {
            language = entry->name;
            return *cached;
        }
    }
    return {};
}

void loadSyntaxAsync(const QString& fileName, bool dark, QObject* context, SyntaxCallback done) {
    if (fileName.isEmpty()) { done({}, {}); return; }
    const auto index = qce::kate::KateSyntaxIndex::load(qce::kate::dataDir());
    // Candidates in priority order, from the first one that is not cached yet.
    QList<Candidate> candidates;
    for (const auto* entry : index.forFileName(fileName)) {
        const QString path = index.filePath(*entry);
        const QString key = cacheKey(path, *entry, dark);
        const auto cached = syntaxCache().constFind(key);
        const bool known = cached != syntaxCache().constEnd();
        if (known && candidates.isEmpty()) {
            if (*cached) { done(*cached, entry->name); return; }
            continue; // Cached as unusable.
        }
        candidates.append({key, path, entry->name, known, known ? *cached : nullptr});
    }
    if (candidates.isEmpty()) { done({}, {}); return; }
    const QString waitKey = candidates.first().key;
    auto& waiting = waitingLoads();
    const bool running = waiting.contains(waitKey);
    waiting[waitKey].append({context, std::move(done)});
    if (running) return;
    const int epoch = cacheEpoch();
    QtConcurrent::run([index, candidates, dark] {
        auto loaded = candidates;
        for (int i = 0; i < loaded.size(); ++i) {
            auto& candidate = loaded[i];
            if (!candidate.known)
                candidate.syntax = std::shared_ptr<const qce::IHighlighter>(KateXmlReader::load(candidate.path, syntaxTheme(dark), index));
            if (candidate.syntax) { loaded.resize(i + 1); break; }
        }
        return loaded;
    }).then(qApp, [waitKey, epoch](const QList<Candidate>& loaded) {
        if (epoch == cacheEpoch()) {
            for (const auto& candidate : loaded)
                if (!candidate.known) syntaxCache().insert(candidate.key, candidate.syntax);
        }
        const bool found = !loaded.isEmpty() && loaded.last().syntax;
        for (const auto& w : waitingLoads().take(waitKey)) {
            if (w.context) w.done(found ? loaded.last().syntax : nullptr, found ? loaded.last().name : QString{});
        }
    });
}

void clearSyntaxCache() {
    ++cacheEpoch();
    syntaxCache().clear();
}
} // namespace diffmerge::gui
