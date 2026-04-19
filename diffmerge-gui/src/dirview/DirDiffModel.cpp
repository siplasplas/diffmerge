#include "DirDiffModel.h"

#include <QDir>
#include <QFileInfo>

namespace diffmerge::gui {

namespace {

// Quick compare: same size + same last-modified time → treat as identical.
// A future version could do a byte-level check, but this is fast enough for
// interactive use.
DirEntryStatus compareFiles(const QFileInfo& left, const QFileInfo& right) {
    if (left.size() == right.size() &&
        left.lastModified() == right.lastModified())
        return DirEntryStatus::Same;
    return DirEntryStatus::Different;
}

void scanRecursive(const QString& leftRoot, const QString& rightRoot,
                   const QString& relDir, int depth,
                   QVector<DirDiffEntry>& out) {
    const QString leftDir  = relDir.isEmpty() ? leftRoot
                                               : leftRoot + QLatin1Char('/') + relDir;
    const QString rightDir = relDir.isEmpty() ? rightRoot
                                               : rightRoot + QLatin1Char('/') + relDir;

    const QDir::Filters filter = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;
    const QDir::SortFlags sort = QDir::Name | QDir::DirsFirst | QDir::IgnoreCase;

    const QFileInfoList leftList  = QDir(leftDir).entryInfoList(filter, sort);
    const QFileInfoList rightList = QDir(rightDir).entryInfoList(filter, sort);

    // Build name sets for fast lookup
    QHash<QString, QFileInfo> leftMap, rightMap;
    for (const QFileInfo& fi : leftList)
        leftMap.insert(fi.fileName().toLower(), fi);
    for (const QFileInfo& fi : rightList)
        rightMap.insert(fi.fileName().toLower(), fi);

    // Merge-iterate by sorted name (dirs first, then files)
    QStringList allNames;
    for (const QFileInfo& fi : leftList)  allNames.append(fi.fileName());
    for (const QFileInfo& fi : rightList) {
        if (!leftMap.contains(fi.fileName().toLower()))
            allNames.append(fi.fileName());
    }
    // Re-sort: dirs first, then files, case-insensitive
    std::sort(allNames.begin(), allNames.end(), [&](const QString& a, const QString& b) {
        const bool aDir = leftMap.value(a.toLower()).isDir() ||
                          rightMap.value(a.toLower()).isDir();
        const bool bDir = leftMap.value(b.toLower()).isDir() ||
                          rightMap.value(b.toLower()).isDir();
        if (aDir != bDir) return aDir > bDir;
        return a.toLower() < b.toLower();
    });
    // Deduplicate
    allNames.removeDuplicates();

    for (const QString& name : allNames) {
        const QString key  = name.toLower();
        const bool hasLeft  = leftMap.contains(key);
        const bool hasRight = rightMap.contains(key);
        const QFileInfo& lfi = hasLeft  ? leftMap[key]  : QFileInfo{};
        const QFileInfo& rfi = hasRight ? rightMap[key] : QFileInfo{};
        const bool isDir = (hasLeft ? lfi.isDir() : rfi.isDir());

        const QString rel = relDir.isEmpty() ? name : relDir + QLatin1Char('/') + name;
        DirDiffEntry entry;
        entry.relativePath = rel;
        entry.leftPath     = hasLeft  ? lfi.absoluteFilePath() : QString{};
        entry.rightPath    = hasRight ? rfi.absoluteFilePath() : QString{};
        entry.depth        = depth;
        entry.isDir        = isDir;

        if (!hasLeft)       entry.status = DirEntryStatus::OnlyRight;
        else if (!hasRight) entry.status = DirEntryStatus::OnlyLeft;
        else if (isDir)     entry.status = DirEntryStatus::Directory;
        else                entry.status = compareFiles(lfi, rfi);

        out.append(entry);

        if (isDir && hasLeft && hasRight)
            scanRecursive(leftRoot, rightRoot, rel, depth + 1, out);
    }
}

}  // namespace

QVector<DirDiffEntry> scanDirDiff(const QString& leftRoot,
                                  const QString& rightRoot) {
    QVector<DirDiffEntry> result;
    scanRecursive(leftRoot, rightRoot, QString{}, 0, result);
    return result;
}

}  // namespace diffmerge::gui
