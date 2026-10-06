#include <diffmerge/DirDiffModel.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringConverter>
#include <diffcore/LineInterner.h>
#include <diffmerge/Comparison.h>
#include <algorithm>
#include <stdexcept>

namespace diffmerge::gui {
namespace {
class Scanner {
public:
    DirectoryScanOptions options;
    diffcore::CancellationToken token;
    DirectoryScanResult result;
    QVector<QRegularExpression> exclusions;
    std::function<void(const QString&,qint64,qint64)> progress;
    void checkpoint() const {
        if (token.isCancellationRequested()) throw diffcore::ComputationStopped(diffcore::StopReason::Cancelled);
    }
    bool excluded(const QString& name) const {
        for (const auto& pattern : exclusions) if (pattern.match(name).hasMatch()) return true;
        return false;
    }
    QFileInfoList list(const QString& path) const {
        if (path.isEmpty()) return {};
        const QFileInfo info(path);
        if (!info.exists()) return {};
        if (!info.isDir() || info.isSymLink() || !info.isReadable())
            throw std::runtime_error(QStringLiteral("Cannot scan directory: %1").arg(path).toStdString());
        return QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    }
    void compare(DirDiffEntry& entry, const QFileInfo& left, const QFileInfo& right) {
        checkpoint();
        const bool normalized = left.size() <= options.maxComparedFileBytes && right.size() <= options.maxComparedFileBytes &&
            (options.ignoreLineEndings || options.diff.ignoreWhitespace || options.diff.ignoreTrailingWhitespace || options.diff.ignoreCase);
        if (left.isSymLink() || right.isSymLink()) {
            entry.contentVerified = true;
            entry.status = left.isSymLink() && right.isSymLink() && left.symLinkTarget() == right.symLinkTarget()
                ? DirEntryStatus::Same : DirEntryStatus::Different;
            return;
        }
        if (!left.isFile() || !right.isFile()) {
            entry.status = DirEntryStatus::Error; entry.diagnostic = QStringLiteral("Unsupported file type"); return;
        }
        if (left.size() != right.size() && !normalized) { entry.status = DirEntryStatus::Different; entry.contentVerified = true; return; }
        QFile a(left.absoluteFilePath()), b(right.absoluteFilePath());
        if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) {
            entry.status = DirEntryStatus::Error;
            entry.diagnostic = QStringLiteral("Cannot read compared files"); return;
        }
        entry.status = DirEntryStatus::Same;
        if (normalized) {
            const auto readBytes = [&](QFile& file, qint64 expected) {
                QByteArray bytes;
                while (bytes.size() < expected) {
                    checkpoint();
                    const auto chunk = file.read(std::min<qint64>(64 * 1024, expected - bytes.size()));
                    if (chunk.isEmpty() || file.error() != QFileDevice::NoError)
                        throw std::runtime_error("File changed or read failed during comparison");
                    bytes.append(chunk);
                    if(progress) progress(entry.relativePath,bytes.size(),expected);
                }
                return bytes;
            };
            QByteArray x, y;
            try { x = readBytes(a, left.size()); y = readBytes(b, right.size()); }
            catch (const std::runtime_error& error) {
                entry.status = DirEntryStatus::Error; entry.diagnostic = QString::fromUtf8(error.what()); return;
            }
            // Binary and invalid UTF-8 data remain byte-exact.
            if (!x.contains('\0') && !y.contains('\0')) {
                QStringDecoder leftDecoder(QStringDecoder::Utf8), rightDecoder(QStringDecoder::Utf8);
                const QString leftText = leftDecoder(x), rightText = rightDecoder(y);
                if (!leftDecoder.hasError() && !rightDecoder.hasError()) {
                    const auto leftSnapshot = TextSnapshot::fromText(leftText), rightSnapshot = TextSnapshot::fromText(rightText);
                    diffcore::ComputationControl control(token);
                    const auto ids = diffcore::LineInterner{}.intern(leftSnapshot.lines, rightSnapshot.lines, options.diff, &control);
                    entry.status = ids.leftIds == ids.rightIds && leftSnapshot.finalNewline == rightSnapshot.finalNewline &&
                        (options.ignoreLineEndings || leftSnapshot.lineEndings == rightSnapshot.lineEndings)
                        ? DirEntryStatus::Same : DirEntryStatus::Different;
                } else entry.status = x == y ? DirEntryStatus::Same : DirEntryStatus::Different;
            } else {
                entry.status = x == y ? DirEntryStatus::Same : DirEntryStatus::Different;
            }
        }
        qint64 read = 0;
        while (!normalized && read < left.size()) {
            checkpoint();
            const auto x = a.read(64 * 1024), y = b.read(64 * 1024);
            if (a.error() != QFileDevice::NoError || b.error() != QFileDevice::NoError || x.isEmpty() || y.isEmpty()) {
                entry.status = DirEntryStatus::Error; entry.diagnostic = QStringLiteral("File changed or read failed during comparison"); return;
            }
            if (x != y) { entry.status = DirEntryStatus::Different; break; }
            read += x.size();
            if(progress) progress(entry.relativePath,read,left.size());
        }
        if (a.size() != left.size() || b.size() != right.size() ||
            QFileInfo(left.absoluteFilePath()).lastModified() != left.lastModified() ||
            QFileInfo(right.absoluteFilePath()).lastModified() != right.lastModified()) {
            entry.status = DirEntryStatus::Error; entry.diagnostic = QStringLiteral("File changed during comparison"); return;
        }
        entry.contentVerified = true;
    }
    void scan(const QString& leftPath, const QString& rightPath, const QString& relative, int depth) {
        checkpoint();
        if (depth > 128) throw std::length_error("Directory nesting limit exceeded");
        QHash<QString, QFileInfo> left, right;
        for (const auto& file : list(leftPath)) { checkpoint(); if (!excluded(file.fileName())) left.insert(file.fileName(), file); }
        for (const auto& file : list(rightPath)) { checkpoint(); if (!excluded(file.fileName())) right.insert(file.fileName(), file); }
        QStringList names = left.keys();
        for (const auto& name : right.keys()) if (!left.contains(name)) names.append(name);
        const auto directory = [&](const QString& name) {
            return (left.value(name).isDir() && !left.value(name).isSymLink()) ||
                   (right.value(name).isDir() && !right.value(name).isSymLink());
        };
        std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
            if (directory(a) != directory(b)) return directory(a);
            const int order = QString::compare(a, b, Qt::CaseInsensitive);
            return order == 0 ? a < b : order < 0;
        });
        for (const auto& name : names) {
            checkpoint();
            if (result.entries.size() >= options.maxEntries) throw std::length_error("Directory entry limit exceeded");
            const bool hasLeft = left.contains(name), hasRight = right.contains(name);
            const auto l = left.value(name), r = right.value(name);
            DirDiffEntry entry;
            entry.relativePath = relative.isEmpty() ? name : relative + '/' + name;
            if(progress) progress(entry.relativePath,0,0);
            entry.leftPath = hasLeft ? l.absoluteFilePath() : QString{};
            entry.rightPath = hasRight ? r.absoluteFilePath() : QString{};
            entry.leftSize = l.size(); entry.rightSize = r.size();
            entry.leftModified = l.lastModified(); entry.rightModified = r.lastModified();
            entry.depth = depth; entry.isDir = directory(name);
            entry.contentVerified = true;
            if (!hasLeft) entry.status = DirEntryStatus::OnlyRight;
            else if (!hasRight) entry.status = DirEntryStatus::OnlyLeft;
            else if (l.isDir() != r.isDir() || l.isSymLink() != r.isSymLink()) entry.status = DirEntryStatus::Different;
            else if (entry.isDir) entry.status = DirEntryStatus::Same;
            else { entry.contentVerified = false; compare(entry, l, r); }
            const int index = result.entries.size();
            result.entries.append(entry);
            const bool collision = hasLeft && hasRight && (l.isDir() != r.isDir() || l.isSymLink() != r.isSymLink());
            if (entry.isDir && !collision) {
                scan(entry.leftPath, entry.rightPath, entry.relativePath, depth+1);
                result.entries[index].emptyDirectory = true;
                for (int i=index+1; i<result.entries.size(); ++i) {
                    if (!result.entries[i].isDir || !result.entries[i].emptyDirectory) result.entries[index].emptyDirectory = false;
                    result.entries[index].contentVerified &= result.entries[i].contentVerified;
                    if (hasLeft && hasRight && result.entries[i].status != DirEntryStatus::Same)
                        result.entries[index].status = DirEntryStatus::Different;
                }
            }
        }
    }
};
}
DirectoryScanResult scanDirectories(const QString& leftRoot, const QString& rightRoot,
    const DirectoryScanOptions& options, const diffcore::CancellationToken& cancellation,
    const std::function<void(const QString&,qint64,qint64)>& progress) {
    Scanner scanner; scanner.options = options; scanner.token = cancellation;
    scanner.progress = progress;
    try {
        if (leftRoot.isEmpty() || rightRoot.isEmpty() || options.maxComparedFileBytes < 0 || options.maxEntries < 0)
            throw std::invalid_argument("Two directory roots and nonnegative limits are required");
        for (const auto& pattern : options.exclusions)
            scanner.exclusions.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern)));
        if (!QFileInfo(leftRoot).isDir() || !QFileInfo(rightRoot).isDir()) throw std::invalid_argument("Two existing directory roots are required");
        scanner.scan(QFileInfo(leftRoot).canonicalFilePath(), QFileInfo(rightRoot).canonicalFilePath(), {}, 0);
        scanner.checkpoint(); scanner.result.status = DirectoryScanStatus::Ready;
    } catch (const diffcore::ComputationStopped&) { scanner.result = {DirectoryScanStatus::Cancelled, {}, QStringLiteral("Directory scan cancelled")}; }
    catch (const std::length_error& error) { scanner.result = {DirectoryScanStatus::ResourceLimit, {}, QString::fromUtf8(error.what())}; }
    catch (const std::exception& error) { scanner.result = {DirectoryScanStatus::Error, {}, QString::fromUtf8(error.what())}; }
    return scanner.result;
}
QVector<DirDiffEntry> scanDirDiff(const QString& leftRoot, const QString& rightRoot) {
    auto result = scanDirectories(leftRoot, rightRoot);
    if (result.status != DirectoryScanStatus::Ready) throw std::runtime_error(result.message.toStdString());
    return result.entries;
}
}
