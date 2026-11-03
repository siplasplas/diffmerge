#include <diffmerge/MergeExport.h>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace diffmerge::gui {
namespace {
struct Unsupported : std::runtime_error { using std::runtime_error::runtime_error; };
QString normalized(const TextSnapshot& text) {
    return text.lines.join('\n') + (text.finalNewline.value_or(false) ? QStringLiteral("\n") : QString{});
}
QByteArray ending(LineEnding value) {
    switch (value) {
        case LineEnding::LF: return "\n";
        case LineEnding::CRLF: return "\r\n";
        case LineEnding::CR: return "\r";
        default: throw std::invalid_argument("Marker ending must be LF, CRLF or CR");
    }
}
QByteArray label(const std::optional<QString>& chosen, const QString& original) {
    const auto text = chosen.value_or(original);
    if (text.contains('\n') || text.contains('\r') || text.contains(QChar(0))) throw std::invalid_argument("Marker labels must be single lines");
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isLowSurrogate()) throw std::invalid_argument("Invalid UTF-16 marker label");
        if (text[i].isHighSurrogate() && (++i >= text.size() || !text[i].isLowSurrogate())) throw std::invalid_argument("Invalid UTF-16 marker label");
    }
    return text.isEmpty() ? QByteArray{} : QByteArray(" ")+text.toUtf8();
}
std::shared_ptr<const PreparedMergeSession> decode(const QByteArray& bytes, const MergeSessionLimits& limits,
    const diffcore::CancellationToken& cancellation, diffcore::ComputationControl& control) {
    MergeSessionInputs inputs; MergeResultSeed seed; seed.file.availability = MergeAvailability::Present; seed.file.bytes = bytes;
    inputs.resultSeed = std::move(seed);
    auto remaining = limits; remaining.maxWork -= control.workPerformed();
    const auto prepared = prepareMergeSession(inputs,remaining,cancellation);
    control.step(prepared.workPerformed);
    if (prepared.status == MergeSessionStatus::Cancelled) throw diffcore::ComputationStopped(diffcore::StopReason::Cancelled);
    if (prepared.status == MergeSessionStatus::ResourceLimit) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
    if (prepared.status == MergeSessionStatus::Unsupported) throw Unsupported(prepared.message.toStdString());
    if (prepared.status != MergeSessionStatus::Ready) throw std::invalid_argument(prepared.message.toStdString());
    return prepared.session;
}
}
PrepareMergeExportResult prepareMergeExport(const MergeExportInput& input, const MergeExportOptions& options,
    const MergeSessionLimits& limits, const diffcore::CancellationToken& cancellation) {
    PrepareMergeExportResult result;
    diffcore::ComputationControl control(cancellation,limits.maxWork);
    try {
        control.step(0);
        MergeExportOutcome outcome; outcome.disposition = options.disposition;
        switch (options.disposition) {
            case MergeExportDisposition::Cancelled:
                result.status = MergeSessionStatus::Ready; result.outcome = outcome; return result;
            case MergeExportDisposition::Draft: case MergeExportDisposition::Resolved: break;
            default: throw std::invalid_argument("Invalid export disposition");
        }
        if (!input.session) throw std::invalid_argument("Export requires a merge session");
        if (options.draftFormat != MergeDraftFormat::Markers && options.draftFormat != MergeDraftFormat::HostBuffer)
            throw std::invalid_argument("Invalid draft format");
        if (options.wholeFileSource && (options.action != MergeFileAction::Keep
            || options.disposition != MergeExportDisposition::Resolved || !options.confirmWholeFileReplacement))
            throw std::invalid_argument("Whole-file replacement requires an explicit resolved Keep decision");
        if (options.disposition == MergeExportDisposition::Resolved && !input.writable)
            throw std::invalid_argument("Read-only sessions cannot export as resolved");
        if (std::uint64_t(input.conflicts.size()) > limits.maxConflicts)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        const auto seed = input.session->inputs().resultSeed.value_or(MergeResultSeed{});
        outcome.action = options.action; outcome.rawPath = options.rawPath.value_or(seed.file.rawPath);
        outcome.mode = options.mode ? options.mode : seed.file.mode; outcome.kind = seed.file.kind;
        outcome.capturedSession = input.session; outcome.fingerprint = seed.fingerprint;
        outcome.conflicts = input.conflicts;
        std::uint64_t metadata = outcome.rawPath.size();
        const auto consume = [&](std::uint64_t size) {
            if (metadata > limits.maxMetadataBytes || size > limits.maxMetadataBytes-metadata)
                throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
            metadata += size; control.step(size);
        };
        consume(seed.fingerprint.size());
        for (const auto* file : {&input.session->inputs().base,&input.session->inputs().ours,&input.session->inputs().theirs,&seed.file}) {
            for (const auto& value : {file->sourceId,file->rawPath,file->objectId}) consume(value.size());
            for (const auto& value : {file->label,file->fileName}) consume(std::uint64_t(value.size())*2);
        }
        for (const auto& text : {options.oursLabel,options.baseLabel,options.theirsLabel}) if (text) consume(std::uint64_t(text->size())*2);
        QSet<QString> ids;
        bool unresolved = false;
        for (const auto& conflict : input.conflicts) {
            consume(std::uint64_t(conflict.id.size())*2);
            if (conflict.id.isEmpty() || ids.contains(conflict.id)) throw std::invalid_argument("Conflict IDs must be nonempty and unique");
            ids.insert(conflict.id);
            switch (conflict.choice) {
                case MergeChoice::Unresolved: case MergeChoice::Ours: case MergeChoice::Theirs:
                case MergeChoice::OursThenTheirs: case MergeChoice::TheirsThenOurs:
                case MergeChoice::Base: case MergeChoice::Delete: case MergeChoice::Manual: break;
                default: throw std::invalid_argument("Invalid conflict choice provenance");
            }
            switch (conflict.state) {
                case MergeResolutionState::Resolved: break;
                case MergeResolutionState::Unresolved: case MergeResolutionState::NeedsReview: unresolved = true; break;
                default: throw std::invalid_argument("Invalid conflict resolution state");
            }
        }
        if (input.session->inputs().hostConflicts) for (const auto& host : *input.session->inputs().hostConflicts) {
            control.step();
            if (!ids.contains(host.id)) throw std::invalid_argument("Export omits a host conflict identity");
        }
        if (options.disposition == MergeExportDisposition::Resolved && !input.session->inputs().hostConflicts
            && !options.confirmUnknownConflictState)
            throw std::invalid_argument("Explicit confirmation is required when host conflict state is unavailable");
        switch (options.action) {
            case MergeFileAction::Delete:
                if (options.disposition != MergeExportDisposition::Resolved || !options.confirmFileDeletion)
                    throw std::invalid_argument("Whole-file deletion requires explicit resolved deletion confirmation");
                for (auto& conflict : outcome.conflicts) { conflict.state = MergeResolutionState::Resolved; conflict.choice = MergeChoice::Delete; conflict.range = {}; conflict.mapped = false; }
                outcome.explicitlyCompleted = true;
                control.step(0); result.status = MergeSessionStatus::Ready; result.outcome = std::move(outcome); return result;
            case MergeFileAction::Keep: break;
            default: throw std::invalid_argument("Invalid export file action");
        }
        if (options.wholeFileSource) {
            switch (*options.wholeFileSource) {
                case MergeSource::Base: case MergeSource::Ours: case MergeSource::Theirs: break;
                default: throw std::invalid_argument("Invalid whole-file source");
            }
            const auto& source = input.session->source(*options.wholeFileSource);
            if (source.availability != MergeAvailability::Present)
                throw std::invalid_argument("Whole-file Keep requires a present source; use explicit Delete for absence");
            const auto output = decode(source.bytes,limits,cancellation,control);
            MarkerImportOptions markerOptions; markerOptions.allowUnconfirmedMarkers = true;
            markerOptions.markerSize = input.markerOptions.markerSize;
            auto remaining = limits; remaining.maxWork -= control.workPerformed();
            const auto markers = importConflictMarkers(*output,markerOptions,remaining,cancellation);
            control.step(markers.workPerformed);
            if (markers.status == MergeSessionStatus::Cancelled) throw diffcore::ComputationStopped(diffcore::StopReason::Cancelled);
            if (markers.status == MergeSessionStatus::ResourceLimit) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
            if (markers.status != MergeSessionStatus::Ready || !markers.conflicts.isEmpty())
                throw std::invalid_argument("Whole-file source contains conflict markers; review it as RESULT first");
            outcome.wholeFileSource = options.wholeFileSource;
            outcome.bytes = source.bytes;
            outcome.mode = options.mode ? options.mode : source.mode;
            outcome.kind = source.kind;
            const auto choice = *options.wholeFileSource == MergeSource::Ours ? MergeChoice::Ours
                : *options.wholeFileSource == MergeSource::Theirs ? MergeChoice::Theirs : MergeChoice::Base;
            for (auto& conflict : outcome.conflicts) {
                conflict.state = MergeResolutionState::Resolved; conflict.choice = choice;
                conflict.range = {}; conflict.mapped = false;
            }
            outcome.serialization = MergeSerialization{output->resultHasUtf8Bom(),output->resultText()->finalNewline.value_or(false),output->resultText()->lineEndings};
            outcome.explicitlyCompleted = true;
            control.step(0); result.status = MergeSessionStatus::Ready; result.outcome = std::move(outcome); return result;
        }
        if (!input.session->resultText()) throw std::invalid_argument("Text export requires an available RESULT seed");
        if (options.disposition == MergeExportDisposition::Draft && options.draftFormat == MergeDraftFormat::HostBuffer) {
            if (options.markerStyle != MergeMarkerStyle::Preserve)
                throw std::invalid_argument("Host-buffer drafts preserve bytes and do not regenerate markers");
            const auto output = decode(input.resultBytes,limits,cancellation,control);
            const auto text = normalized(*output->resultText());
            for (const auto& conflict : input.conflicts) {
                control.step();
                if (!conflict.mapped) continue; // Retain ambiguity for subsequent host review.
                const auto range = conflict.range;
                if (range.start < 0 || range.length < 0 || range.start > text.size() || range.length > text.size()-range.start)
                    throw std::invalid_argument("Invalid mapped host-buffer conflict range");
                for (const int offset : {range.start,range.start+range.length})
                    if (offset > 0 && offset < text.size() && text[offset].isLowSurrogate() && text[offset-1].isHighSurrogate())
                        throw std::invalid_argument("Conflict range splits a Unicode character");
            }
            outcome.draftFormat = MergeDraftFormat::HostBuffer;
            outcome.bytes = input.resultBytes;
            outcome.serialization = MergeSerialization{output->resultHasUtf8Bom(),output->resultText()->finalNewline.value_or(false),output->resultText()->lineEndings};
            control.step(0); result.status = MergeSessionStatus::Ready; result.outcome = std::move(outcome); return result;
        }
        if (options.disposition == MergeExportDisposition::Resolved && unresolved)
            throw std::invalid_argument("Resolve or review every conflict before exporting as resolved");
        auto current = decode(input.resultBytes,limits,cancellation,control);
        const auto text = normalized(*current->resultText());
        QVector<int> lineOffsets{0};
        for (int i = 0; i < text.size(); ++i) { control.step(); if (text[i] == '\n') lineOffsets.append(i+1); }
        lineOffsets.append(text.size());
        QVector<int> order;
        for (int i = 0; i < input.conflicts.size(); ++i) {
            control.step(); const auto& conflict = input.conflicts[i]; const auto range = conflict.range;
            if (!conflict.mapped || range.start < 0 || range.length < 0 || range.start > text.size() || range.length > text.size()-range.start)
                throw std::invalid_argument("Review unmapped or invalid conflict ranges before export");
            for (const int offset : {range.start,range.start+range.length})
                if (offset > 0 && offset < text.size() && text[offset].isLowSurrogate() && text[offset-1].isHighSurrogate())
                    throw std::invalid_argument("Conflict range splits a Unicode character");
            order.append(i);
        }
        std::sort(order.begin(),order.end(),[&](int a,int b) { control.step(); return input.conflicts[a].range.start < input.conflicts[b].range.start; });
        int lastEnd = 0;
        for (const int index : order) {
            const auto range = input.conflicts[index].range;
            if (range.start < lastEnd) throw std::invalid_argument("Overlapping conflict ranges cannot be exported");
            lastEnd = range.start+range.length;
        }
        auto markerOptions = input.markerOptions; markerOptions.allowUnconfirmedMarkers = true;
        // Literal marker consent is recorded in original coordinates. Translate
        // only untouched literal lines; callers must review edited literal markers.
        if (!markerOptions.literalMarkerLines.isEmpty() && input.resultBytes != seed.file.bytes && !input.literalMarkerLinesReviewed)
            throw std::invalid_argument("Review literal marker lines in current coordinates before exporting edited text");
        auto remaining = limits; remaining.maxWork -= control.workPerformed();
        const auto imported = importConflictMarkers(*current,markerOptions,remaining,cancellation);
        control.step(imported.workPerformed);
        if (imported.status == MergeSessionStatus::Cancelled) throw diffcore::ComputationStopped(diffcore::StopReason::Cancelled);
        if (imported.status == MergeSessionStatus::ResourceLimit) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        if (imported.status != MergeSessionStatus::Ready) throw std::invalid_argument(imported.message.toStdString());
        QVector<int> markerForConflict(input.conflicts.size(),-1);
        for (int i = 0; i < imported.conflicts.size(); ++i) {
            control.step(); const auto& marker = imported.conflicts[i];
            const int start = lineOffsets[marker.resultLines.start], end = lineOffsets[marker.resultLines.end()];
            bool matched = false;
            for (int j = 0; j < input.conflicts.size(); ++j) {
                control.step(); const auto& conflict = input.conflicts[j];
                if (conflict.range.start != start || conflict.range.length != end-start) continue;
                if (conflict.state == MergeResolutionState::Resolved) throw std::invalid_argument("A resolved conflict still contains markers");
                markerForConflict[j] = i; matched = true; break;
            }
            if (!matched) throw std::invalid_argument("RESULT contains an untracked marker conflict; import and review it first");
        }
        for (int i = 0; i < input.conflicts.size(); ++i)
            if (input.conflicts[i].state != MergeResolutionState::Resolved && markerForConflict[i] < 0)
                throw std::invalid_argument("Draft export requires complete markers for every unresolved fragment; manual edits are retained");
        outcome.bytes = input.resultBytes;
        switch (options.markerStyle) {
            case MergeMarkerStyle::Preserve: break;
            case MergeMarkerStyle::Merge: case MergeMarkerStyle::Diff3: case MergeMarkerStyle::ZDiff3: {
                if (options.markerSize < 1 || std::uint64_t(options.markerSize) > limits.maxLineCodeUnits)
                    throw std::invalid_argument("Export marker length is outside the line limit");
                const auto eol = ending(options.markerEnding);
                // Replace only delimiters around the current draft fragments,
                // preserving manual edits and common text outside zdiff3 regions.
                for (int i = imported.conflicts.size()-1; i >= 0; --i) {
                    control.step(); const auto& marker = imported.conflicts[i];
                    if (options.markerStyle != MergeMarkerStyle::Merge && !marker.base)
                        throw std::invalid_argument("Diff3/zdiff3 export requires an available BASE fragment");
                    QByteArray block = QByteArray(options.markerSize,'<')+label(options.oursLabel,marker.oursLabel)+eol+marker.ours;
                    if (options.markerStyle != MergeMarkerStyle::Merge)
                        block += QByteArray(options.markerSize,'|')+label(options.baseLabel,marker.baseLabel)+eol+*marker.base;
                    block += QByteArray(options.markerSize,'=')+eol+marker.theirs;
                    block += QByteArray(options.markerSize,'>')+label(options.theirsLabel,marker.theirsLabel);
                    const auto& original = input.resultBytes;
                    const auto end = marker.resultBytes.start+marker.resultBytes.length;
                    if (end > 0 && (original[end-1] == '\r' || original[end-1] == '\n')) block += eol;
                    if (std::uint64_t(block.size()) > limits.maxInputBytes || std::uint64_t(outcome.bytes.size()-marker.resultBytes.length+block.size()) > limits.maxInputBytes)
                        throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
                    control.step(block.size());
                    const int start = lineOffsets[marker.resultLines.start], finish = lineOffsets[marker.resultLines.end()];
                    const auto snapshot = TextSnapshot::fromText(QString::fromUtf8(block));
                    const int length = normalized(snapshot).size(), delta = length-(finish-start);
                    for (auto& conflict : outcome.conflicts) {
                        if (conflict.range.start == start && conflict.range.length == finish-start) conflict.range.length = length;
                        else if (conflict.range.start >= finish) conflict.range.start += delta;
                    }
                    outcome.bytes.replace(marker.resultBytes.start,marker.resultBytes.length,block);
                }
                break;
            }
            default: throw std::invalid_argument("Invalid marker export style");
        }
        const auto output = decode(outcome.bytes,limits,cancellation,control);
        outcome.serialization = MergeSerialization{output->resultHasUtf8Bom(),output->resultText()->finalNewline.value_or(false),output->resultText()->lineEndings};
        outcome.explicitlyCompleted = options.disposition == MergeExportDisposition::Resolved;
        control.step(0); result.status = MergeSessionStatus::Ready; result.outcome = std::move(outcome);
    } catch (const diffcore::ComputationStopped& stopped) {
        result.status = stopped.reason == diffcore::StopReason::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::ResourceLimit;
        result.message = QString::fromUtf8(stopped.what());
    } catch (const Unsupported& error) {
        result.status = MergeSessionStatus::Unsupported; result.message = QString::fromUtf8(error.what());
    } catch (const std::bad_alloc&) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QStringLiteral("Merge export allocation failed");
    } catch (const std::length_error& error) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QString::fromUtf8(error.what());
    } catch (const std::exception& error) {
        result.status = MergeSessionStatus::Error; result.message = QString::fromUtf8(error.what());
    }
    return result;
}
} // namespace diffmerge::gui
