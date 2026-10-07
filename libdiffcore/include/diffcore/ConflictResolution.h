#pragma once
#include "ConflictMarkers.h"
#include <QJsonObject>
#include <functional>

namespace diffcore {
enum class ResolutionPolicy { Replay, Conservative };
enum class ResolutionTarget { Right, Left };
enum class DecisionState { AutomaticExact, AutomaticPolicy, NeedsReview, Reviewed, Deferred };
struct ResolutionOptions {
    ResolutionPolicy policy = ResolutionPolicy::Replay;
    ResolutionTarget target = ResolutionTarget::Right;
    double minimumSideOverlap = 0.80, maximumTargetOverlap = 0.35;
    int minimumRewriteTokens = 24;
};
struct ResolutionCandidate {
    QString id, title;
    QByteArray replacement;
    QStringList assumptions;
    QVector<ByteRange> sourceSlices;
};
struct ConflictDecision {
    QString id, rule, explanation;
    DecisionState state = DecisionState::NeedsReview;
    QStringList reasons;
    std::optional<QByteArray> replacement; // Empty is deletion; absent is undecided.
    QVector<ByteRange> sourceSlices;
    QVector<ResolutionCandidate> candidates;
    QJsonObject metrics;
};
struct ResolutionPlan {
    ConflictStatus status = ConflictStatus::Error;
    ParsedConflictFile input;
    ResolutionOptions options;
    QVector<ConflictDecision> decisions;
    QString message;
    quint64 workPerformed = 0;
};
struct OutputPiece {
    ByteRange output;
    std::optional<ByteRange> input; // Absent for reviewed/synthesized text.
    QString conflictId;
};
struct MaterializedResolution {
    ConflictStatus status = ConflictStatus::Error;
    QByteArray bytes;
    QVector<ByteRange> conflictRanges;
    QVector<OutputPiece> provenance;
    QStringList deferredIds;
    QString message;
    bool clean = false;
};
using ResolutionProgress = std::function<void(int completed, int total)>;
ResolutionPlan planConflictResolution(const QByteArray& input, const MarkerOptions& markers = {},
    const ResolutionOptions& options = {}, const ConflictLimits& limits = {},
    const CancellationToken& cancellation = {}, const ResolutionProgress& progress = {});
MaterializedResolution materializeResolution(const ResolutionPlan& plan, bool reviewDraft = false,
    const ConflictLimits& limits = {}, const CancellationToken& cancellation = {});
// Copies preserve evidence. Digest/ID checks prevent applying stale decisions.
bool reviewConflictDecision(ResolutionPlan& plan, const QByteArray& inputDigest,
    const QString& conflictId, const QByteArray& replacement, bool deferred = false,
    QString* error = nullptr);
QJsonObject resolutionReport(const ResolutionPlan& plan, const MaterializedResolution& output,
    const std::optional<MaterializedResolution>& draft = std::nullopt);
QString decisionStateName(DecisionState state);
QString conflictStatusName(ConflictStatus status);
} // namespace diffcore
