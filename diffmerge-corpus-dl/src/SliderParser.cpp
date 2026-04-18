#include "SliderParser.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace diffmerge::corpus {

static const QLatin1StringView kGithubPrefix{"https://github.com"};
static const QLatin1StringView kRawPrefix   {"https://raw.githubusercontent.com"};

QString parseRepoPath(const QString& infoLine) {
    QString line = infoLine.trimmed();
    if (!line.startsWith(kGithubPrefix)) return {};
    QString path = line.mid(kGithubPrefix.size());
    if (path.endsWith(QLatin1StringView(".git")))
        path.chop(4);
    return path;  // "/user/repo"
}

SliderEntry parseSliderLine(const QString& line, const QString& repoPath) {
    // Format: "hash0:path0 hash1:path1 sign blockBegin delta"
    const QStringList tokens = line.trimmed().split(QLatin1Char(' '),
                                                     Qt::SkipEmptyParts);
    if (tokens.size() < 5) return {};

    SliderEntry e;
    for (int i = 0; i < 2; ++i) {
        const int colon = tokens[i].indexOf(QLatin1Char(':'));
        if (colon < 0) return {};
        const QString hash = tokens[i].left(colon);
        const QString path = tokens[i].mid(colon + 1);
        e.url[i] = QString(kRawPrefix) + repoPath + QLatin1Char('/') + hash
                 + QLatin1Char('/') + path;
        if (i == 0)
            e.ext = QFileInfo(path).suffix().prepend(QLatin1Char('.'));
    }
    const QString signStr = tokens[2];
    e.sign       = signStr.isEmpty() ? '+' : signStr[0].toLatin1();
    e.blockBegin = tokens[3].toInt();
    e.delta      = tokens[4].toInt();
    return e;
}

QString loadInfoFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    return parseRepoPath(in.readLine());
}

QVector<SliderEntry> loadSlidersFile(const QString& path, const QString& repoPath) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QVector<SliderEntry> entries;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        SliderEntry e = parseSliderLine(line, repoPath);
        if (!e.url[0].isEmpty())
            entries.append(e);
    }
    return entries;
}

}  // namespace diffmerge::corpus
