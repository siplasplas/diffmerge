#pragma once

#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <diffcore/ComputationControl.h>
#include <diffcore/DiffTypes.h>
#include <functional>

namespace diffmerge::gui {
enum class DirEntryStatus { OnlyLeft, OnlyRight, Same, Different, Directory, Error };
struct DirDiffEntry {
    QString relativePath, leftPath, rightPath;
    DirEntryStatus status = DirEntryStatus::Same;
    bool isDir = false;
    int depth = 0;
    qint64 leftSize = 0, rightSize = 0;
    QDateTime leftModified, rightModified;
    bool contentVerified = false;
    QString diagnostic;
    bool emptyDirectory = false;
};
struct DirectoryScanOptions {
    QStringList exclusions{QStringLiteral(".git"), QStringLiteral("build"), QStringLiteral("build-*"),
        QStringLiteral("cmake-build-*"), QStringLiteral("node_modules"), QStringLiteral("__pycache__")};
    qint64 maxComparedFileBytes = 64 * 1024 * 1024;
    int maxEntries = 100000;
    bool ignoreLineEndings = false;
    // Same text in different encodings (or with/without BOM) counts as the same.
    bool ignoreEncoding = false;
    diffcore::DiffOptions diff{};
};
enum class DirectoryScanStatus { Ready, Cancelled, ResourceLimit, Error };
struct DirectoryScanResult {
    DirectoryScanStatus status = DirectoryScanStatus::Error;
    QVector<DirDiffEntry> entries;
    QString message;
};
// Owned results, no GUI objects; symbolic-link directories are never followed.
DirectoryScanResult scanDirectories(const QString& leftRoot, const QString& rightRoot,
    const DirectoryScanOptions& options = {}, const diffcore::CancellationToken& cancellation = {},
    const std::function<void(const QString&, qint64, qint64)>& progress = {});
// Compatibility adapter. Throws on cancellation, resource limits or scan failure.
QVector<DirDiffEntry> scanDirDiff(const QString& leftRoot, const QString& rightRoot);
}
