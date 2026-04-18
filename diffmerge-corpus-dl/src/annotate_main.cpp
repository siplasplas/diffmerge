#include <climits>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

// ---- System diff output parsing ----------------------------------------

struct SysDiffHunk {
    char type;          // 'a'=insert, 'd'=delete, 'c'=change
    int  leftStart;     // 1-based start in file A
    int  rightStart;    // 1-based start in file B
};

// Parse normal-format diff output (e.g. "3a4", "2,5d1", "3,4c5,6").
static QVector<SysDiffHunk> parseDiffNormal(const QString& output) {
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d+)(?:,\d+)?([adc])(\d+)(?:,\d+)?$)"));
    QVector<SysDiffHunk> hunks;
    for (const QString& raw : output.split(QLatin1Char('\n'))) {
        const auto m = re.match(raw.trimmed());
        if (!m.hasMatch()) continue;
        SysDiffHunk h;
        h.leftStart  = m.captured(1).toInt();
        h.type       = m.captured(2)[0].toLatin1();
        h.rightStart = m.captured(3).toInt();
        hunks.append(h);
    }
    return hunks;
}

// Find the system-diff hunk of the right type nearest to blockBegin.
// sign='+' → 'a' hunks, position = rightStart (inserted block in B).
// sign='-' → 'd' hunks, position = leftStart  (deleted block in A).
// Returns -1 if no matching hunk exists.
static int findSysDiffPos(const QVector<SysDiffHunk>& hunks,
                           char sign, int blockBegin) {
    int bestPos  = -1;
    int bestDist = INT_MAX;
    for (const SysDiffHunk& h : hunks) {
        if (sign == '+' && h.type != 'a') continue;
        if (sign == '-' && h.type != 'd') continue;
        const int pos  = (sign == '+') ? h.rightStart : h.leftStart;
        const int dist = std::abs(pos - blockBegin);
        if (dist < bestDist) { bestDist = dist; bestPos = pos; }
    }
    return bestPos;
}

// ---- File helpers -------------------------------------------------------

static QString findPairFile(const QString& repoDir,
                             const QString& digest, int side) {
    const QString prefix = digest + QLatin1Char('_') + QString::number(side);
    const QStringList cands =
        QDir(repoDir).entryList({prefix + QStringLiteral(".*")}, QDir::Files);
    if (cands.isEmpty()) return {};
    return repoDir + QLatin1Char('/') + cands.first();
}

// ---- CSV line annotation ------------------------------------------------

// Parse and rewrite one CSV line, appending diffPos to each slider field.
// If a slider already has 4 parts (already annotated), keeps existing value.
// Returns the rewritten line, or empty string to skip the line.
static QString annotateLine(const QString& line,
                             const QString& repoDir,
                             const QVector<SysDiffHunk>& hunks) {
    const QStringList fields = line.trimmed().split(QLatin1Char(','));
    if (fields.size() < 2) return {};
    const QString digest = fields[0].trimmed();
    if (digest.isEmpty()) return {};

    QStringList out;
    out.append(digest);
    for (int i = 1; i < fields.size(); ++i) {
        const QStringList parts = fields[i].trimmed().split(
            QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 3) { out.append(fields[i].trimmed()); continue; }

        const char sign       = parts[0].isEmpty() ? '+' : parts[0][0].toLatin1();
        const int  blockBegin = parts[1].toInt();
        const int  delta      = parts[2].toInt();
        if (blockBegin <= 0) { out.append(fields[i].trimmed()); continue; }

        const int diffPos = (parts.size() >= 4)
                            ? parts[3].toInt()  // keep existing annotation
                            : findSysDiffPos(hunks, sign, blockBegin);

        out.append(QStringLiteral("%1 %2 %3 %4")
                   .arg(QChar(sign)).arg(blockBegin).arg(delta).arg(diffPos));
    }
    return out.join(QLatin1Char(','));
}

// ---- Entry point --------------------------------------------------------

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("corpus-annotate"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Annotate corpus CSV files with classic system-diff slider positions.\n"
                       "Runs 'diff' (Myers, no heuristics) on each file pair and stores\n"
                       "the matched hunk position as a 4th field in each slider entry."));
    parser.addHelpOption();

    QCommandLineOption corpusOpt(
        QStringLiteral("corpus"),
        QStringLiteral("Path to corpusDiff directory (output of corpus-dl)."),
        QStringLiteral("path"), QStringLiteral("corpusDiff"));
    QCommandLineOption dryRunOpt(
        QStringLiteral("dry-run"),
        QStringLiteral("Print annotated CSV to stdout instead of overwriting files."));
    QCommandLineOption reposOpt(
        QStringLiteral("repos"),
        QStringLiteral("Process at most N repositories (0 = all)."),
        QStringLiteral("N"), QStringLiteral("0"));
    parser.addOption(corpusOpt);
    parser.addOption(dryRunOpt);
    parser.addOption(reposOpt);
    parser.process(app);

    const QString corpusDir = parser.value(corpusOpt);
    const bool    dryRun    = parser.isSet(dryRunOpt);
    const int     repoLimit = parser.value(reposOpt).toInt();

    QDir dir(corpusDir);
    if (!dir.exists()) {
        QTextStream(stderr) << "directory not found: " << corpusDir << '\n';
        return 2;
    }

    QStringList csvFiles = dir.entryList({QStringLiteral("*.csv")},
                                          QDir::Files, QDir::Name);
    if (repoLimit > 0 && csvFiles.size() > repoLimit)
        csvFiles = csvFiles.mid(0, repoLimit);

    QTextStream out(stdout);
    out << "Annotating " << csvFiles.size() << " CSV file(s) in: " << corpusDir << "\n\n";

    int filesDone = 0, slidersAnnotated = 0;
    for (const QString& csvName : csvFiles) {
        const QString csvPath = dir.filePath(csvName);
        const QString repoDir = dir.filePath(QFileInfo(csvName).baseName());

        QFile f(csvPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);

        // Load all lines; group by digest to run diff only once per file pair.
        struct Entry { QString digest; QString rawLine; };
        QVector<Entry> entries;
        while (!in.atEnd()) {
            const QString raw = in.readLine();
            if (raw.trimmed().isEmpty()) continue;
            const QString digest = raw.section(QLatin1Char(','), 0, 0).trimmed();
            entries.append({digest, raw});
        }
        f.close();

        // Annotate each entry.
        QStringList newLines;
        QString lastDigest;
        QVector<SysDiffHunk> hunks;
        for (const Entry& e : entries) {
            if (e.digest != lastDigest) {
                lastDigest = e.digest;
                const QString pathA = findPairFile(repoDir, e.digest, 0);
                const QString pathB = findPairFile(repoDir, e.digest, 1);
                hunks.clear();
                if (!pathA.isEmpty() && !pathB.isEmpty()) {
                    QProcess proc;
                    proc.start(QStringLiteral("diff"), {pathA, pathB});
                    proc.waitForFinished(30000);
                    hunks = parseDiffNormal(proc.readAllStandardOutput());
                }
            }
            const QString annotated = annotateLine(e.rawLine, repoDir, hunks);
            if (!annotated.isEmpty()) {
                newLines.append(annotated);
                ++slidersAnnotated;
            }
        }

        if (dryRun) {
            out << "=== " << csvName << " ===\n";
            for (const QString& l : newLines) out << l << '\n';
        } else {
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream(stderr) << "cannot write: " << csvPath << '\n';
                continue;
            }
            QTextStream ts(&f);
            for (const QString& l : newLines) ts << l << '\n';
        }
        ++filesDone;
        out << '[' << filesDone << '/' << csvFiles.size() << "] " << csvName << '\n';
        out.flush();
    }

    out << "\nDone. " << filesDone << " CSV files, "
        << slidersAnnotated << " entries annotated.\n";
    return 0;
}
