#pragma once

#include <QFileInfo>
#include <QString>
#include <QVector>

namespace diffmerge::gui {

enum class DirEntryStatus {
    OnlyLeft,    // exists only in left directory
    OnlyRight,   // exists only in right directory
    Same,        // identical (same size + mtime, or byte-equal)
    Different,   // exists on both sides but differs
    Directory,   // subdirectory (contents may differ)
};

struct DirDiffEntry {
    QString relativePath;   // relative to the root dirs, uses '/' separator
    QString leftPath;       // absolute; empty when OnlyRight
    QString rightPath;      // absolute; empty when OnlyLeft
    DirEntryStatus status;
    bool isDir = false;
    int depth  = 0;         // nesting level (0 = root entry)
};

// Recursively compares two directory trees and returns a flat, depth-first
// ordered list of entries suitable for display in a tree view.
QVector<DirDiffEntry> scanDirDiff(const QString& leftRoot,
                                  const QString& rightRoot);

}  // namespace diffmerge::gui
