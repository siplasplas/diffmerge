#pragma once
#include <QStringList>
namespace diffmerge::gui {
enum class LaunchKind { Empty, PrefillFile, PrefillDirectory, Files, Directories, Error };
struct LaunchOptions {
    LaunchKind kind = LaunchKind::Empty;
    QStringList paths, labels;
    QString error;
};
LaunchOptions validateLaunchPaths(const QStringList& paths, const QStringList& labels = {});
}
