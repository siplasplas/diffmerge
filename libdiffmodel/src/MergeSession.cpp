#include <diffmerge/MergeSession.h>
#include <QStringDecoder>
#include <QSet>
#include <climits>
#include <stdexcept>

namespace diffmerge::gui {
namespace {
int index(MergeSource source) {
    switch (source) {
        case MergeSource::Base: return 0;
        case MergeSource::Ours: return 1;
        case MergeSource::Theirs: return 2;
    }
    throw std::invalid_argument("Invalid merge source");
}
struct Unsupported : std::runtime_error { using std::runtime_error::runtime_error; };
void consume(std::uint64_t amount, std::uint64_t limit, std::uint64_t& total) {
    if (amount > limit - total) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
    total += amount;
}
struct Budget {
    std::uint64_t bytes = 0, metadata = 0, lines = 0, units = 0;
};
std::optional<TextSnapshot> decode(const MergeFileInput& file, const MergeSessionLimits& limits,
    Budget& budget, bool& bom, diffcore::ComputationControl& control) {
    control.step();
    for (const auto& bytes : {file.sourceId, file.rawPath, file.objectId}) {
        consume(bytes.size(), limits.maxMetadataBytes, budget.metadata); control.step(bytes.size());
    }
    for (const auto& text : {file.label, file.fileName}) {
        consume(std::uint64_t(text.size()) * 2, limits.maxMetadataBytes, budget.metadata); control.step(text.size());
    }
    switch (file.kind) {
        case MergeFileKind::RegularFile: case MergeFileKind::SymbolicLink: case MergeFileKind::Submodule: case MergeFileKind::Other: break;
        default: throw std::invalid_argument("Invalid merge file kind");
    }
    switch (file.availability) {
        case MergeAvailability::Absent:
        case MergeAvailability::Unknown:
            if (!file.bytes.isEmpty()) throw std::invalid_argument("Absent or unknown merge input contains bytes");
            return std::nullopt;
        case MergeAvailability::Present: break;
        default: throw std::invalid_argument("Invalid merge input availability");
    }
    switch (file.kind) {
        case MergeFileKind::RegularFile: break;
        case MergeFileKind::SymbolicLink: case MergeFileKind::Submodule: case MergeFileKind::Other:
            throw Unsupported("This merge input is not a regular text file");
        default: throw std::invalid_argument("Invalid merge file kind");
    }
    consume(file.bytes.size(), limits.maxInputBytes, budget.bytes);
    if (file.bytes.size() > INT_MAX - 3) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
    bom = file.bytes.startsWith(QByteArray::fromHex("efbbbf"));
    QString text;
    // Decode bounded, complete UTF-8 chunks; Stateless rejects incomplete tails.
    for (qsizetype offset = bom ? 3 : 0; offset < file.bytes.size();) {
        qsizetype end = std::min<qsizetype>(offset + 4096, file.bytes.size());
        if (end < file.bytes.size()) {
            int continuation = 0;
            while (end > offset && (static_cast<unsigned char>(file.bytes[end]) & 0xc0) == 0x80) {
                control.step(); --end;
                if (++continuation > 3) throw Unsupported("Merge input is not valid UTF-8; host conversion is required");
            }
        }
        const auto chunk = QByteArrayView(file.bytes).sliced(offset, end - offset);
        control.step(chunk.size());
        for (char byte : chunk) if (byte == '\0') throw Unsupported("Binary merge input cannot be edited as text");
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
        const QString decoded = decoder(chunk);
        if (decoder.hasError()) throw Unsupported("Merge input is not valid UTF-8; host conversion is required");
        consume(decoded.size(), limits.maxInputCodeUnits, budget.units);
        text += decoded;
        offset = end;
    }
    TextSnapshot snapshot;
    snapshot.label = file.label; snapshot.fileName = file.fileName; snapshot.finalNewline = false;
    int start = 0;
    const auto append = [&](int end, LineEnding ending) {
        consume(1, limits.maxInputLines, budget.lines);
        if (std::uint64_t(end - start) > limits.maxLineCodeUnits)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        snapshot.lines.append(text.mid(start, end - start)); snapshot.lineEndings.append(ending);
    };
    for (int i = 0; i < text.size(); ++i) {
        control.step();
        if (std::uint64_t(i - start) > limits.maxLineCodeUnits)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        if (text[i] != '\r' && text[i] != '\n') continue;
        const bool crlf = text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n';
        append(i, crlf ? LineEnding::CRLF : text[i] == '\r' ? LineEnding::CR : LineEnding::LF);
        if (crlf) ++i;
        start = i + 1;
    }
    if (start < text.size()) append(text.size(), LineEnding::None);
    else if (!text.isEmpty()) snapshot.finalNewline = true;
    return snapshot;
}
void validateRange(const std::optional<diffcore::LineRange>& range, const std::optional<TextSnapshot>& text) {
    if (!range) return;
    if (!text || range->start < 0 || range->count < 0 || range->start > text->lines.size()
        || range->count > text->lines.size() - range->start)
        throw std::invalid_argument("Conflict range is outside an available input snapshot");
}
} // namespace
const MergeFileInput& PreparedMergeSession::source(MergeSource source) const {
    switch (index(source)) {
        case 0: return m_inputs.base;
        case 1: return m_inputs.ours;
        default: return m_inputs.theirs;
    }
}
const std::optional<TextSnapshot>& PreparedMergeSession::sourceText(MergeSource source) const { return m_text[index(source)]; }
bool PreparedMergeSession::hasUtf8Bom(MergeSource source) const { return m_bom[index(source)]; }
PrepareMergeSessionResult prepareMergeSession(const MergeSessionInputs& inputs,
    const MergeSessionLimits& limits, const diffcore::CancellationToken& cancellation) {
    const auto start = std::chrono::steady_clock::now();
    PrepareMergeSessionResult result;
    diffcore::ComputationControl control(cancellation, limits.maxWork);
    QString phase;
    try {
        control.step(0);
        if (inputs.hostConflicts && std::uint64_t(inputs.hostConflicts->size()) > limits.maxConflicts)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        auto prepared = std::shared_ptr<PreparedMergeSession>(new PreparedMergeSession);
        Budget budget;
        const std::array<const MergeFileInput*, 4> files{&inputs.base, &inputs.ours, &inputs.theirs,
            inputs.resultSeed ? &inputs.resultSeed->file : nullptr};
        const std::array<QString, 4> names{QStringLiteral("BASE"), QStringLiteral("OURS"), QStringLiteral("THEIRS"), QStringLiteral("RESULT")};
        for (int i = 0; i < 4; ++i) if (files[i]) {
            phase = names[i];
            prepared->m_text[i] = decode(*files[i], limits, budget, prepared->m_bom[i], control);
        }
        phase = QStringLiteral("Conflict metadata");
        if (inputs.resultSeed) {
            consume(inputs.resultSeed->fingerprint.size(), limits.maxMetadataBytes, budget.metadata);
            control.step(inputs.resultSeed->fingerprint.size());
        }
        if (inputs.resultSeed) switch (inputs.resultSeed->origin) {
            case ResultSeedOrigin::WorkingFile: case ResultSeedOrigin::HostMergeBuffer: case ResultSeedOrigin::RetainedResult: break;
            default: throw std::invalid_argument("Invalid result seed origin");
        }
        QSet<QString> ids;
        if (inputs.hostConflicts) for (const auto& conflict : *inputs.hostConflicts) {
            consume(std::uint64_t(conflict.id.size()) * 2, limits.maxMetadataBytes, budget.metadata);
            control.step(conflict.id.size() + 1);
            if (conflict.id.isEmpty() || ids.contains(conflict.id)) throw std::invalid_argument("Conflict IDs must be nonempty and unique");
            ids.insert(conflict.id);
            switch (conflict.state) {
                case MergeResolutionState::Unresolved: case MergeResolutionState::Resolved: case MergeResolutionState::NeedsReview: break;
                default: throw std::invalid_argument("Invalid conflict resolution state");
            }
            validateRange(conflict.base, prepared->m_text[0]); validateRange(conflict.ours, prepared->m_text[1]);
            validateRange(conflict.theirs, prepared->m_text[2]); validateRange(conflict.result, prepared->m_text[3]);
        }
        prepared->m_inputs = inputs;
        control.step(0);
        result.session = std::move(prepared); result.status = MergeSessionStatus::Ready;
    } catch (const diffcore::ComputationStopped& stopped) {
        result.status = stopped.reason == diffcore::StopReason::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::ResourceLimit;
        result.message = QString::fromUtf8(stopped.what());
    } catch (const Unsupported& error) {
        result.status = MergeSessionStatus::Unsupported; result.message = QString::fromUtf8(error.what());
    } catch (const std::bad_alloc&) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QStringLiteral("Merge session allocation failed");
    } catch (const std::length_error& error) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QString::fromUtf8(error.what());
    } catch (const std::exception& error) {
        result.status = MergeSessionStatus::Error; result.message = QString::fromUtf8(error.what());
    }
    if (!phase.isEmpty() && !result.message.isEmpty()) result.message.prepend(phase + QStringLiteral(": "));
    result.workPerformed = control.workPerformed(); result.preparationTime = std::chrono::steady_clock::now() - start;
    return result;
}
} // namespace diffmerge::gui
