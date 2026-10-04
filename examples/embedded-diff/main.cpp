#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMainWindow>
#include <QSplitter>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>

#include <diffmerge/DirDiffWidget.h>
#include <diffmerge/FileDiffWidget.h>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow window;
    auto* panes = new QSplitter(Qt::Vertical, &window);
    auto* history = new QTreeWidget(panes);
    history->setHeaderLabels({"Revision", "Subject"});
    auto* first = new QTreeWidgetItem(history, {"parent", "Before formatting"});
    auto* second = new QTreeWidgetItem(history, {"current", "Format and add a call"});
    auto* diff = new diffmerge::gui::FileDiffWidget(panes);
    diff->setPathBarVisible(false);
    diff->setNavigationBarVisible(false);
    // Shortcut policy belongs to the host, never to the diff component.
    auto* next = new QShortcut(Qt::Key_F7, diff);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(next, &QShortcut::activated, diff,
                     &diffmerge::gui::FileDiffWidget::navigateToNext);
    auto* previous = new QShortcut(Qt::SHIFT | Qt::Key_F7, diff);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(previous, &QShortcut::activated, diff,
                     &diffmerge::gui::FileDiffWidget::navigateToPrev);
    QObject::connect(history, &QTreeWidget::currentItemChanged, diff,
        [diff, second](QTreeWidgetItem* selected) {
            const QStringList before{"a:=2;", "finish();"};
            const QStringList after{"prepare();", "a := 2;", "finish();"};
            diff->setContent(before, selected == second ? after : before);
        });
    history->setCurrentItem(first);
    history->setCurrentItem(second);
    window.setCentralWidget(panes);
    window.resize(900, 600);
    window.show();

    if (app.arguments().contains("--smoke")) {
        // Exercise a second public widget and resources from the static archive.
        diffmerge::gui::DirDiffWidget directories;
        if (!QFile::exists(":/icons/folder.svg") ||
            QIcon(":/icons/folder.svg").pixmap(16, 16).isNull()) return 1;
        QTimer::singleShot(0, &app, &QApplication::quit);
    }
    return app.exec();
}
