#include "LaunchOptions.h"
#include <QFileInfo>
#include <QFile>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
namespace diffmerge::gui {
QString validateMergeLaunch(const MergeLaunchOptions& options) {
    if (options.resultPath.isEmpty()) return QStringLiteral("--merge requires -o MERGED with an existing RESULT seed");
    if (options.labels.size()>4) return QStringLiteral("Merge accepts at most four labels: BASE, LOCAL, REMOTE, RESULT");
    if (options.markerSize<1 || options.markerSize>200000) return QStringLiteral("--marker-size must be between 1 and 200000");
    if (options.baseAbsent && options.basePath!=QStringLiteral("-")) return QStringLiteral("Use BASE '-' with --base-absent; an existing empty BASE is not absent");
    QStringList sources{options.localPath,options.remotePath};
    if (!options.baseAbsent) sources.prepend(options.basePath);
    for (const auto& path : sources+QStringList{options.resultPath}) {
        const QFileInfo file(path);
        if (path.isEmpty() || !file.exists() || !file.isFile() || file.isSymLink())
            return QStringLiteral("Merge requires existing regular files, not symbolic links: %1").arg(path);
    }
    const QFileInfo result(options.resultPath);
    for (const auto& path : sources) {
        if (QFileInfo(path).canonicalFilePath()==result.canonicalFilePath())
            return QStringLiteral("MERGED must be separate from BASE, LOCAL and REMOTE");
#ifdef Q_OS_UNIX
        struct stat source{}, output{};
        if (::lstat(QFile::encodeName(path).constData(),&source)==0 && ::lstat(QFile::encodeName(options.resultPath).constData(),&output)==0
            && source.st_dev==output.st_dev && source.st_ino==output.st_ino)
            return QStringLiteral("MERGED must not alias an input file through a hard link");
#endif
    }
    return {};
}
LaunchOptions validateLaunchPaths(const QStringList& paths,const QStringList& labels) {
    LaunchOptions result; result.paths=paths; result.labels=labels;
    const auto fail=[&](const QString& message) { result.kind=LaunchKind::Error; result.error=message; return result; };
    if(paths.size()>2 || labels.size()>2) return fail(QStringLiteral("Supply at most two paths and two labels"));
    if(paths.isEmpty()) return result;
    if(paths.size()==1) {
        result.kind=QFileInfo(paths[0]).isDir() ? LaunchKind::PrefillDirectory : LaunchKind::PrefillFile;
        return result;
    }
    for(const auto& path:paths) {
        const QFileInfo file(path);
        if(!file.exists()) return fail(QStringLiteral("Path does not exist: %1").arg(path));
        if(!file.isFile() && !file.isDir()) return fail(QStringLiteral("Unsupported path type: %1").arg(path));
    }
    const bool left=QFileInfo(paths[0]).isDir(), right=QFileInfo(paths[1]).isDir();
    if(left!=right) return fail(QStringLiteral("%1 is a %2; %3 is a %4: compare two files or two directories")
        .arg(paths[0],left ? QStringLiteral("directory") : QStringLiteral("file"),paths[1],right ? QStringLiteral("directory") : QStringLiteral("file")));
    result.kind=left ? LaunchKind::Directories : LaunchKind::Files;
    return result;
}
}
