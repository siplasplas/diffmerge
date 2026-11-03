#pragma once

#include <array>
#include <optional>
#include <QByteArray>
#include <diffmerge/Comparison.h>

namespace diffmerge::gui {

enum class MergeSource { Base, Ours, Theirs };
enum class MergeAvailability { Present, Absent, Unknown };
enum class MergeFileKind { RegularFile, SymbolicLink, Submodule, Other };
enum class ResultSeedOrigin { WorkingFile, HostMergeBuffer, RetainedResult };
enum class MergeResolutionState { Unresolved, Resolved, NeedsReview };

// Opaque host identities. No filesystem reads, object lookup or interpretation.
struct MergeFileInput {
    MergeAvailability availability = MergeAvailability::Unknown;
    MergeFileKind kind = MergeFileKind::RegularFile;
    QByteArray bytes; // Authoritative, owned bytes; present text must be UTF-8.
    QByteArray sourceId, rawPath, objectId;
    std::optional<std::uint32_t> mode;
    QString label, fileName; // Display label and syntax hint, independent of rawPath.
};
struct MergeResultSeed {
    MergeFileInput file;
    ResultSeedOrigin origin = ResultSeedOrigin::HostMergeBuffer;
    QByteArray fingerprint; // Opaque token to return to the host on future export.
};
struct MergeConflict {
    QString id; // Stable, unique within this session.
    std::optional<diffcore::LineRange> base{}, ours{}, theirs{}, result{};
    MergeResolutionState state = MergeResolutionState::Unresolved;
};
struct MergeSessionInputs {
    MergeFileInput base, ours, theirs;
    std::optional<MergeResultSeed> resultSeed; // Missing seed never becomes empty text.
    // nullopt = no authoritative conflict list; empty = host explicitly reports none.
    std::optional<QVector<MergeConflict>> hostConflicts;
};
struct MergeSessionLimits {
    std::uint64_t maxInputBytes = 32 * 1024 * 1024; // Aggregate, including RESULT.
    std::uint64_t maxMetadataBytes = 1024 * 1024; // Identities, paths, labels and conflict IDs.
    std::uint64_t maxInputLines = 100000;
    std::uint64_t maxInputCodeUnits = 4000000;
    std::uint64_t maxLineCodeUnits = 200000;
    std::uint64_t maxConflicts = 100000;
    std::uint64_t maxWork = 20000000;
};
enum class MergeSessionStatus { Ready, Cancelled, ResourceLimit, Error, Unsupported };
class PreparedMergeSession;
struct PrepareMergeSessionResult {
    MergeSessionStatus status = MergeSessionStatus::Error;
    std::shared_ptr<const PreparedMergeSession> session; // Non-null only for Ready.
    QString message;
    std::chrono::nanoseconds preparationTime{};
    std::uint64_t workPerformed = 0;
};

// GUI-independent preparation for inspection and subsequent resolution. It does
// not compute a merge, infer resolved states, parse markers or manufacture RESULT.
PrepareMergeSessionResult prepareMergeSession(const MergeSessionInputs& inputs,
    const MergeSessionLimits& limits = {}, const diffcore::CancellationToken& cancellation = {});

class PreparedMergeSession {
public:
    const MergeSessionInputs& inputs() const { return m_inputs; }
    const MergeFileInput& source(MergeSource source) const;
    // Absent/unknown files have no snapshot; an existing empty file has one.
    const std::optional<TextSnapshot>& sourceText(MergeSource source) const;
    const std::optional<TextSnapshot>& resultText() const { return m_text[3]; }
    bool hasUtf8Bom(MergeSource source) const;
    bool resultHasUtf8Bom() const { return m_bom[3]; }
private:
    PreparedMergeSession() = default;
    MergeSessionInputs m_inputs;
    std::array<std::optional<TextSnapshot>, 4> m_text;
    std::array<bool, 4> m_bom{};
    friend PrepareMergeSessionResult prepareMergeSession(const MergeSessionInputs&,
        const MergeSessionLimits&, const diffcore::CancellationToken&);
};
} // namespace diffmerge::gui
