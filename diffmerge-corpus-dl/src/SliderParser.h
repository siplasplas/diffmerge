#pragma once

#include <QString>
#include <QVector>

namespace diffmerge::corpus {

struct SliderEntry {
    QString url[2];
    QString ext;
    char    sign       = '+';
    int     blockBegin = 0;
    int     delta      = 0;
};

// Extract the "/user/repo" path from a raw .info file content line
// e.g. "https://github.com/AFNetworking/AFNetworking.git" → "/AFNetworking/AFNetworking"
// Returns empty string on unrecognized format.
QString parseRepoPath(const QString& infoLine);

// Parse one slider line into a SliderEntry, constructing raw.githubusercontent URLs.
// repoPath must be in the form "/user/repo" (as returned by parseRepoPath).
// Returns a default-constructed entry with empty urls on parse failure.
SliderEntry parseSliderLine(const QString& line, const QString& repoPath);

// File-level helpers (thin wrappers around the pure functions above).
QString     loadInfoFile(const QString& path);
QVector<SliderEntry> loadSlidersFile(const QString& path, const QString& repoPath);

}  // namespace diffmerge::corpus
