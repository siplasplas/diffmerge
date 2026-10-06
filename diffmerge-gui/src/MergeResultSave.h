#pragma once
#include <diffmerge/MergeExport.h>
#include <QDateTime>
#include <QFileDevice>
#include <functional>

namespace diffmerge::gui::desktop {
enum class MergeSaveStatus { Saved, Cancelled, Stale, Unsupported, Error };
struct MergeSaveStamp {
    QString path, canonicalPath;
    QByteArray fingerprint;
    qint64 size = 0;
    QDateTime modified;
    QFileDevice::Permissions permissions;
    quint64 device = 0, inode = 0;
    bool hasIdentity = false;
};
struct MergeFileReadResult {
    MergeSaveStatus status = MergeSaveStatus::Error;
    QByteArray bytes;
    std::optional<MergeSaveStamp> stamp;
    QString message;
};
struct MergeFileSaveResult {
    MergeSaveStatus status = MergeSaveStatus::Error;
    std::optional<MergeSaveStamp> stamp;
    std::optional<MergeExportOutcome> outcome;
    QString message;
};
using MergeSaveProgress = std::function<void(qint64,qint64)>;
// Desktop-only filesystem helpers. The embedded widget never calls these.
MergeFileReadResult readMergeResultFile(const QString& path,
    const MergeSessionLimits& limits = {}, const diffcore::CancellationToken& cancellation = {});
MergeFileSaveResult saveMergeResultFile(const MergeExportInput& input, const MergeExportOptions& options,
    const MergeSaveStamp& expected, const MergeSessionLimits& limits = {},
    const diffcore::CancellationToken& cancellation = {}, const MergeSaveProgress& progress = {});
} // namespace diffmerge::gui::desktop
