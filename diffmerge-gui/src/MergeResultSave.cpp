#include "MergeResultSave.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace diffmerge::gui::desktop {
namespace {
std::optional<MergeSaveStamp> stamp(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink()) return std::nullopt;
    MergeSaveStamp value;
    value.path = info.absoluteFilePath(); value.canonicalPath = info.canonicalFilePath();
    value.size = info.size(); value.modified = info.lastModified(); value.permissions = info.permissions();
#ifdef Q_OS_UNIX
    struct stat identity{};
    if (::lstat(QFile::encodeName(value.path).constData(),&identity) != 0 || !S_ISREG(identity.st_mode)) return std::nullopt;
    value.hasIdentity = true; value.device = identity.st_dev; value.inode = identity.st_ino;
#endif
    return value;
}
bool sameMetadata(const MergeSaveStamp& a, const MergeSaveStamp& b) {
    return a.path == b.path && a.canonicalPath == b.canonicalPath && a.size == b.size
        && a.modified == b.modified && a.permissions == b.permissions && a.hasIdentity == b.hasIdentity
        && (!a.hasIdentity || (a.device == b.device && a.inode == b.inode));
}
}
bool sameMergeSaveStamp(const MergeSaveStamp& a, const MergeSaveStamp& b) {
    return sameMetadata(a,b) && a.fingerprint == b.fingerprint;
}
MergeFileReadResult readMergeResultFile(const QString& path, const MergeSessionLimits& limits,
    const diffcore::CancellationToken& cancellation) {
    MergeFileReadResult result;
    const auto cancelled = [&] {
        if (!cancellation.isCancellationRequested()) return false;
        result.status = MergeSaveStatus::Cancelled; result.message = QStringLiteral("Merge file read cancelled"); return true;
    };
    if (cancelled()) return result;
    const auto before = stamp(path);
    if (!before) { result.status = MergeSaveStatus::Unsupported; result.message = QStringLiteral("Select an existing regular file; symbolic links are not supported"); return result; }
    if (before->size < 0 || std::uint64_t(before->size) > limits.maxInputBytes) { result.message = QStringLiteral("File exceeds the merge byte limit"); return result; }
    QFile file(before->path);
    if (!file.open(QIODevice::ReadOnly)) { result.message = file.errorString(); return result; }
#ifdef Q_OS_UNIX
    struct stat opened{};
    if (::fstat(file.handle(),&opened) != 0 || !S_ISREG(opened.st_mode) || quint64(opened.st_dev) != before->device || quint64(opened.st_ino) != before->inode) {
        result.status = MergeSaveStatus::Stale; result.message = QStringLiteral("File changed while opening"); return result;
    }
#endif
    QByteArray bytes; QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancelled()) return result;
        const auto block = file.read(65536);
        if (file.error() != QFileDevice::NoError || block.isEmpty()) { result.message = QStringLiteral("Could not read merge input"); return result; }
        if (std::uint64_t(block.size()) > limits.maxInputBytes-std::uint64_t(bytes.size())) { result.message = QStringLiteral("File exceeds the merge byte limit"); return result; }
        bytes += block; hash.addData(block);
    }
    const auto after = stamp(before->path);
    if (!after || !sameMetadata(*before,*after) || bytes.size() != before->size) {
        result.status = MergeSaveStatus::Stale; result.message = QStringLiteral("File changed while reading"); return result;
    }
    if (cancelled()) return result;
    result.status = MergeSaveStatus::Saved; result.bytes = std::move(bytes); result.stamp = *after; result.stamp->fingerprint = hash.result();
    return result;
}
MergeFileSaveResult saveMergeResultFile(const MergeExportInput& input, const MergeExportOptions& options,
    const MergeSaveStamp& expected, const MergeSessionLimits& limits,
    const diffcore::CancellationToken& cancellation, const MergeSaveProgress& progress) {
    MergeFileSaveResult result;
    const auto cancelled = [&] {
        if (!cancellation.isCancellationRequested()) return false;
        result.status = MergeSaveStatus::Cancelled; result.message = QStringLiteral("Merge save cancelled"); return true;
    };
    if (cancelled()) return result;
    if (!input.writable || options.disposition == MergeExportDisposition::Cancelled || options.action != MergeFileAction::Keep) {
        result.status = MergeSaveStatus::Unsupported; result.message = QStringLiteral("Desktop saving requires a writable Keep result"); return result;
    }
    const auto exported = prepareMergeExport(input,options,limits,cancellation);
    if (exported.status != MergeSessionStatus::Ready || !exported.outcome) {
        result.status = exported.status == MergeSessionStatus::Cancelled ? MergeSaveStatus::Cancelled : MergeSaveStatus::Error;
        result.message = exported.message; return result;
    }
    const auto& outcome = *exported.outcome;
    if (QFileInfo(QFile::decodeName(outcome.rawPath)).absoluteFilePath() != expected.path
        || outcome.kind != MergeFileKind::RegularFile || outcome.mode != input.session->inputs().resultSeed->file.mode) {
        result.status = MergeSaveStatus::Unsupported; result.message = QStringLiteral("Desktop saving does not change the output path, file kind or mode"); return result;
    }
    const auto validate = [&] {
        const auto current = readMergeResultFile(expected.path,limits,cancellation);
        if (current.status != MergeSaveStatus::Saved || !current.stamp) {
            result.status = current.status; result.message = current.message; return false;
        }
        if (!sameMergeSaveStamp(*current.stamp,expected)) { result.status = MergeSaveStatus::Stale; result.message = QStringLiteral("The output file changed since it was loaded or last saved. Reload and review before saving."); return false; }
        if (!(current.stamp->permissions & (QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther)) || !QFileInfo(expected.path).isWritable()) {
            result.message = QStringLiteral("The output file is read-only"); return false;
        }
        return true;
    };
    if (progress) progress(0,outcome.bytes.size());
    if (!validate()) return result;
    QSaveFile file(expected.path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { result.message = file.errorString(); return result; }
    if (!file.setPermissions(expected.permissions)) { result.message = QStringLiteral("Could not preserve output file permissions"); return result; }
    for (qint64 offset = 0; offset < outcome.bytes.size();) {
        if (cancelled()) { file.cancelWriting(); return result; }
        const qint64 count = std::min<qint64>(65536,outcome.bytes.size()-offset);
        const auto written = file.write(outcome.bytes.constData()+offset,count);
        if (written <= 0) { result.message = file.errorString(); return result; }
        offset += written; if (progress) progress(offset,outcome.bytes.size());
    }
    if (!validate() || cancelled()) { file.cancelWriting(); return result; }
    // QSaveFile performs the atomic replacement. Cancellation after a successful
    // commit cannot retroactively turn a saved result into a cancelled result.
    if (!file.commit()) { result.message = file.errorString(); return result; }
    // Verify the committed content before acknowledging it. Use a fresh token:
    // a late cancellation cannot undo an already completed atomic replacement.
    const auto saved = readMergeResultFile(expected.path,limits);
    if (saved.status != MergeSaveStatus::Saved || !saved.stamp
        || saved.bytes != outcome.bytes || saved.stamp->permissions != expected.permissions) {
        result.status = MergeSaveStatus::Stale;
        result.message = QStringLiteral("The file was replaced, but changed before save verification. Edits remain in memory; reload and review before further saving."); return result;
    }
    result.stamp = saved.stamp;
    result.status = MergeSaveStatus::Saved; result.outcome = outcome; return result;
}
} // namespace diffmerge::gui::desktop
