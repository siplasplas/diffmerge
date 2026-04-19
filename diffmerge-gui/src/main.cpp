#include <QApplication>
#include <QFileInfo>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DiffMerge"));
    QApplication::setOrganizationName(QStringLiteral("DiffMerge"));

    diffmerge::gui::MainWindow w;

    const QStringList args = QApplication::arguments();
    // Only the first two extra arguments are considered; the rest are ignored.
    const int n = std::min(static_cast<int>(args.size()) - 1, 2);

    if (n == 1) {
        const QString p = args.at(1);
        if (QFileInfo(p).isDir())
            w.prefillDirs(p);
        else
            w.prefillFiles(p);
    } else if (n == 2) {
        const QString p1 = args.at(1);
        const QString p2 = args.at(2);
        const bool p1Dir = QFileInfo(p1).isDir();
        const bool p2Dir = QFileInfo(p2).isDir();

        if (p1Dir && p2Dir) {
            w.loadDirectories(p1, p2);       // both dirs — scan immediately
        } else if (!p1Dir && !p2Dir) {
            w.loadFiles(p1, p2);             // both files — diff immediately
        } else {
            // mixed types: first arg decides the view, second pre-filled only
            if (p1Dir)
                w.prefillDirs(p1, p2);
            else
                w.prefillFiles(p1, p2);
        }
    }

    w.show();
    return QApplication::exec();
}
