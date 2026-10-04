#include <QApplication>
#include <QFutureWatcher>
#include <QStatusBar>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QDebug>
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
    // These belong to the host: the component has no worker or job identity.
    quint64 generation = 0;
    diffcore::CancellationToken cancellation;
    const bool smoke = app.arguments().contains("--smoke");
    QObject::connect(&app, &QApplication::aboutToQuit, &window, [&] {
        cancellation.requestCancellation();
    });
    QObject::connect(history, &QTreeWidget::currentItemChanged, diff,
        [&, diff, second](QTreeWidgetItem* selected) {
            cancellation.requestCancellation();
            cancellation = diffcore::CancellationToken{};
            const auto token = cancellation;
            const auto job = ++generation;
            using namespace diffmerge::gui;
            const auto before = TextSnapshot::fromText("a:=2;\nfinish();\n", "Parent revision", "sample.cpp");
            const auto after = selected == second
                ? TextSnapshot::fromText("prepare();\na := 2;\nfinish();", "Selected revision", "sample.cpp") : before;
            auto* watcher = new QFutureWatcher<PrepareResult>(&window);
            QObject::connect(watcher, &QFutureWatcher<PrepareResult>::finished, diff, [&, watcher, job] {
                const auto result = watcher->result();
                watcher->deleteLater();
                // Cancelled/stale results never replace a newer comparison.
                if (job != generation) return;
                if (result.status != PreparationStatus::Ready) {
                    window.statusBar()->showMessage(result.message);
                    if (smoke) app.exit(1);
                    return;
                }
                diff->setComparison(result.comparison);
                diff->revealText(Side::Right, {0, 0, 0}, true);
                diff->setSearchHighlights(Side::Right, {{0, 0, 3}});
                qInfo() << "Preparation ms:" << result.preparationTime.count() / 1e6
                        << "installation ms:" << diff->lastInstallationTime().count() / 1e6;
                if (smoke) app.quit();
            });
            watcher->setFuture(QtConcurrent::run([before, after, token] {
                return prepareComparison(before, after, {}, token);
            }));
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
        QTimer::singleShot(5000, &app, [&app] { app.exit(1); });
    }
    const int exitCode = app.exec();
    cancellation.requestCancellation();
    QThreadPool::globalInstance()->waitForDone();
    return exitCode;
}
