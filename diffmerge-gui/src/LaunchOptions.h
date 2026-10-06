#pragma once
#include <QStringList>
namespace diffmerge::gui {
enum class LaunchKind { Empty, PrefillFile, PrefillDirectory, Files, Directories, Error };
struct MergeLaunchOptions {
    QString basePath, localPath, remotePath, resultPath;
    QStringList labels; // BASE, LOCAL, REMOTE, RESULT.
    int markerSize = 7;
    bool baseAbsent = false, readOnly = false;
};
struct LaunchOptions {
    LaunchKind kind = LaunchKind::Empty;
    QStringList paths, labels;
    QString error;
};
QString validateMergeLaunch(const MergeLaunchOptions& options);
LaunchOptions validateLaunchPaths(const QStringList& paths, const QStringList& labels = {});
}
