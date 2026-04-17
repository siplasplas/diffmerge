#include "FileLoader.h"

#include <QFile>
#include <QTextStream>

namespace diffmerge::cli {

LoadResult loadTextFile(const QString& path) {
    LoadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.errorMessage = QStringLiteral("cannot open '%1': %2")
                             .arg(path, f.errorString());
        return r;
    }
    QTextStream in(&f);
    // QIODevice::Text mode already converts CRLF -> LF on read, so
    // QTextStream::readLine() gives us the raw line without the
    // terminator. A trailing newline does not yield an empty last line.
    while (!in.atEnd()) {
        r.lines.append(in.readLine());
    }
    r.ok = true;
    return r;
}

}  // namespace diffmerge::cli
