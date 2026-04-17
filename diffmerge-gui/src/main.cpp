#include <QApplication>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DiffMerge"));
    QApplication::setOrganizationName(QStringLiteral("DiffMerge"));

    diffmerge::gui::MainWindow w;

    // If two file paths were passed on the command line, load them
    // immediately so the user lands in the diff view.
    const QStringList args = QApplication::arguments();
    if (args.size() >= 3) {
        w.loadFiles(args.at(1), args.at(2));
    }

    w.show();
    return QApplication::exec();
}
