// Offscreen screenshot helper - loads two files, renders the diff widget,
// and saves a PNG. Used for visual verification without a display.

#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "../src/fileview/FileDiffWidget.h"

static QStringList loadLines(const QString& path) {
    QStringList lines;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return lines;
    QTextStream in(&f);
    while (!in.atEnd()) lines.append(in.readLine());
    return lines;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    if (argc < 4) {
        qWarning("Usage: screenshot LEFT RIGHT OUT.png");
        return 1;
    }
    QStringList left = loadLines(QString::fromLocal8Bit(argv[1]));
    QStringList right = loadLines(QString::fromLocal8Bit(argv[2]));

    diffmerge::gui::FileDiffWidget w;
    w.resize(1200, 200);
    w.setContent(left, right);
    w.show();

    // Process events so layout settles, then grab.
    QTimer::singleShot(500, &app, [&]() {
        QPixmap pm = w.grab();
        pm.save(QString::fromLocal8Bit(argv[3]));
        app.quit();
    });
    return app.exec();
}
