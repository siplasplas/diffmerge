#include "CorpusProcessor.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QVector>

#include "Downloader.h"
#include "SliderParser.h"

namespace diffmerge::corpus {

// SHA-256 of both URLs concatenated, first 12 hex characters.
static QString digestName(const SliderEntry& e) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(e.url[0].toUtf8());
    hash.addData(e.url[1].toUtf8());
    return QString::fromLatin1(hash.result().toHex()).left(12);
}

// "/user/repo" → "user&repo"  (safe directory name)
static QString repoToDir(const QString& repoPath) {
    QString s = repoPath;
    if (s.startsWith(QLatin1Char('/'))) s.remove(0, 1);
    s.replace(QLatin1Char('/'), QLatin1Char('&'));
    return s;
}

CorpusProcessor::CorpusProcessor(const QString& corpusDir,
                                 const QString& outputDir)
    : m_corpusDir(corpusDir), m_outputDir(outputDir) {}

void CorpusProcessor::run() {
    QDir dir(m_corpusDir);
    if (!dir.exists()) {
        QTextStream(stderr) << "corpus dir not found: " << m_corpusDir << '\n';
        return;
    }

    QStringList infos = dir.entryList({QStringLiteral("*.info")},
                                       QDir::Files, QDir::Name);
    QDir().mkpath(m_outputDir);

    QTextStream out(stdout);
    for (int i = 0; i < infos.size(); ++i) {
        out << "[" << (i + 1) << "/" << infos.size() << "] "
            << infos[i] << '\n';
        out.flush();
        processRepo(dir.filePath(infos[i]), i + 1, infos.size());
    }
}

void CorpusProcessor::processRepo(const QString& infoPath, int idx, int total) {
    const QString repoPath = loadInfoFile(infoPath);
    if (repoPath.isEmpty()) {
        QTextStream(stderr) << "skip (not a github URL): " << infoPath << '\n';
        return;
    }

    // Derive sliders file: afnetworking.info → afnetworking-human.sliders
    const QFileInfo fi(infoPath);
    const QString slidersPath = fi.dir().filePath(
        fi.baseName() + QStringLiteral("-human.sliders"));

    const QVector<SliderEntry> entries = loadSlidersFile(slidersPath, repoPath);
    if (entries.isEmpty()) {
        QTextStream(stderr) << "skip (no sliders): " << slidersPath << '\n';
        return;
    }

    const QString dirName  = repoToDir(repoPath);
    const QString outDir   = m_outputDir + QLatin1Char('/') + dirName;
    const QString csvPath  = m_outputDir + QLatin1Char('/') + dirName
                           + QStringLiteral(".csv");
    QDir().mkpath(outDir);

    // digest → list of all SliderEntry that map to the same file pair
    QHash<QString, QVector<SliderEntry>> byDigest;

    Downloader dl;
    QTextStream out(stdout);
    int downloaded = 0, skipped = 0;

    for (int i = 0; i < entries.size(); ++i) {
        const SliderEntry& e = entries[i];
        const QString digest = digestName(e);

        if (byDigest.contains(digest)) {
            // Duplicate pair — just record the metadata, no download needed.
            byDigest[digest].append(e);
            continue;
        }

        // First occurrence: download both files.
        bool ok = true;
        for (int side = 0; side < 2; ++side) {
            const QString outFile = outDir + QLatin1Char('/') + digest
                                  + QLatin1Char('_') + QString::number(side)
                                  + e.ext;
            if (!dl.download(e.url[side], outFile)) {
                ok = false;
                break;
            }
        }

        if (ok) {
            byDigest[digest] = {e};
            ++downloaded;
        } else {
            ++skipped;
        }

        out << "  " << (i + 1) << "/" << entries.size()
            << "  dl=" << downloaded << " skip=" << skipped << '\r';
        out.flush();
    }
    out << '\n';

    // Write CSV: digest,sign blockBegin delta,...
    QFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(stderr) << "cannot write CSV: " << csvPath << '\n';
        return;
    }
    QTextStream csvOut(&csv);

    // Sort keys for reproducible output.
    QStringList digests = byDigest.keys();
    std::sort(digests.begin(), digests.end());

    for (const QString& digest : digests) {
        const QVector<SliderEntry>& vec = byDigest[digest];
        // Sort by blockBegin like korpusy does.
        QVector<SliderEntry> sorted = vec;
        std::sort(sorted.begin(), sorted.end(),
                  [](const SliderEntry& a, const SliderEntry& b) {
                      return a.blockBegin < b.blockBegin;
                  });
        csvOut << digest;
        for (const SliderEntry& s : sorted)
            csvOut << ',' << s.sign << ' ' << s.blockBegin << ' ' << s.delta;
        csvOut << '\n';
    }

    out << "  repo " << dirName << ": " << downloaded << " pairs downloaded"
        << (skipped ? QStringLiteral(", %1 skipped").arg(skipped) : QString())
        << '\n';
}

}  // namespace diffmerge::corpus
