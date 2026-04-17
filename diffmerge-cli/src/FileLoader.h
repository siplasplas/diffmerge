// Loads a text file into a QStringList, one entry per line.
// CRLF/LF are both handled; the trailing newline does not produce
// an empty final line.

#ifndef DIFFMERGE_CLI_FILELOADER_H
#define DIFFMERGE_CLI_FILELOADER_H

#include <QString>
#include <QStringList>

namespace diffmerge::cli {

struct LoadResult {
    bool ok = false;
    QString errorMessage;  // Non-empty iff ok == false
    QStringList lines;
};

LoadResult loadTextFile(const QString& path);

}  // namespace diffmerge::cli

#endif  // DIFFMERGE_CLI_FILELOADER_H
