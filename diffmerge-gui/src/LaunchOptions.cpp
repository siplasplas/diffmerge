#include "LaunchOptions.h"
#include <QFileInfo>
namespace diffmerge::gui {
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
