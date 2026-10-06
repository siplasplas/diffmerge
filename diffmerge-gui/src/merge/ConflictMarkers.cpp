#include <diffmerge/ConflictMarkers.h>
#include <algorithm>
#include <stdexcept>

namespace diffmerge::gui {
namespace {
enum class Delimiter { None, Ours, Base, Separator, Theirs };
Delimiter delimiter(const QString& line, int size, diffcore::ComputationControl& control) {
    if (line.isEmpty()) return Delimiter::None;
    const auto first = line[0];
    if (first != '<' && first != '|' && first != '=' && first != '>') return Delimiter::None;
    int count = 0;
    while (count < line.size() && line[count] == first) { control.step(); ++count; }
    if (count < size) return Delimiter::None;
    if (count != size || (count < line.size() && line[count] != ' ' && line[count] != '\t'))
        throw std::invalid_argument("Malformed marker length or label delimiter");
    if (first == '=' && count != line.size()) throw std::invalid_argument("A separator marker cannot have a label");
    if (first == '<') return Delimiter::Ours;
    if (first == '|') return Delimiter::Base;
    if (first == '>') return Delimiter::Theirs;
    return Delimiter::Separator;
}
} // namespace
MarkerImportResult importConflictMarkers(const PreparedMergeSession& session,
    const MarkerImportOptions& options, const MergeSessionLimits& limits,
    const diffcore::CancellationToken& cancellation) {
    MarkerImportResult result;
    diffcore::ComputationControl control(cancellation, limits.maxWork);
    int lineNumber = -1;
    try {
        control.step(0);
        if (options.markerSize < 1 || std::uint64_t(options.markerSize) > limits.maxLineCodeUnits)
            throw std::invalid_argument("Marker size must be positive and within the line limit");
        if (!session.resultText() || !session.inputs().resultSeed)
            throw std::invalid_argument("Marker import requires an available RESULT text seed");
        const auto& text = *session.resultText();
        const auto& bytes = session.inputs().resultSeed->file.bytes;
        if (std::uint64_t(bytes.size()) > limits.maxInputBytes || std::uint64_t(text.lines.size()) > limits.maxInputLines)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        if (std::uint64_t(options.literalMarkerLines.size()) > limits.maxInputLines)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        for (int line : options.literalMarkerLines) {
            control.step();
            if (line < 0 || line >= text.lines.size()) throw std::invalid_argument("Literal marker line is outside RESULT");
        }
        enum class State { Outside, Ours, Base, Theirs };
        State state = State::Outside;
        QVector<ImportedConflict> conflicts;
        ImportedConflict current;
        qint64 offset = session.resultHasUtf8Bom() ? 3 : 0;
        qint64 fragmentStart = 0;
        std::uint64_t metadata = 0, units = 0;
        const auto label = [&](const QString& line) {
            const auto value = line.mid(options.markerSize).trimmed();
            const auto size = std::uint64_t(value.size()) * 2;
            if (size > limits.maxMetadataBytes - metadata) throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
            metadata += size;
            return value;
        };
        for (lineNumber = 0; lineNumber < text.lines.size(); ++lineNumber) {
            control.step();
            const auto& line = text.lines[lineNumber];
            if (std::uint64_t(line.size()) > limits.maxLineCodeUnits)
                throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
            const auto ending = text.lineEndings[lineNumber];
            const auto lineUnits = std::uint64_t(line.size()) + (ending == LineEnding::CRLF ? 2 : ending == LineEnding::None ? 0 : 1);
            if (lineUnits > limits.maxInputCodeUnits - units)
                throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
            units += lineUnits;
            // Count original bytes without round-tripping decoded text.
            const qint64 start = offset;
            while (offset < bytes.size() && bytes[offset] != '\r' && bytes[offset] != '\n') { control.step(); ++offset; }
            if (offset < bytes.size()) {
                const char ending = bytes[offset++];
                if (ending == '\r' && offset < bytes.size() && bytes[offset] == '\n') ++offset;
            }
            const auto marker = options.literalMarkerLines.contains(lineNumber) ? Delimiter::None : delimiter(line, options.markerSize, control);
            if (marker == Delimiter::None) continue;
            if (state == State::Outside) {
                if (marker != Delimiter::Ours) throw std::invalid_argument("Marker outside a conflict block");
                if (!options.allowUnconfirmedMarkers && (!session.inputs().hostConflicts || session.inputs().hostConflicts->isEmpty()))
                    throw std::invalid_argument("Marker recognition requires host conflict metadata or explicit marker-file consent");
                if (std::uint64_t(conflicts.size()) >= limits.maxConflicts)
                    throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
                current = {}; current.id = QStringLiteral("marker:%1").arg(start);
                current.resultLines.start = lineNumber; current.resultBytes.start = start;
                current.oursLabel = label(line); fragmentStart = offset; state = State::Ours;
            } else if (state == State::Ours && marker == Delimiter::Base) {
                current.ours = bytes.mid(fragmentStart, start - fragmentStart);
                current.baseLabel = label(line); fragmentStart = offset; state = State::Base;
            } else if ((state == State::Ours || state == State::Base) && marker == Delimiter::Separator) {
                const auto fragment = bytes.mid(fragmentStart, start - fragmentStart);
                if (state == State::Ours) current.ours = fragment; else current.base = fragment;
                fragmentStart = offset; state = State::Theirs;
            } else if (state == State::Theirs && marker == Delimiter::Theirs) {
                current.theirs = bytes.mid(fragmentStart, start - fragmentStart);
                current.theirsLabel = label(line);
                current.resultLines.count = lineNumber + 1 - current.resultLines.start;
                current.resultBytes.length = offset - current.resultBytes.start;
                conflicts.append(std::move(current)); state = State::Outside;
            } else throw std::invalid_argument("Nested, repeated or out-of-order conflict marker");
        }
        if (state != State::Outside) throw std::invalid_argument("Incomplete conflict block at end of RESULT");
        control.step(0);
        result.conflicts = std::move(conflicts); result.status = MergeSessionStatus::Ready;
    } catch (const diffcore::ComputationStopped& stopped) {
        result.status = stopped.reason == diffcore::StopReason::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::ResourceLimit;
        result.message = QString::fromUtf8(stopped.what());
    } catch (const std::bad_alloc&) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QStringLiteral("Marker import allocation failed");
    } catch (const std::length_error& error) {
        result.status = MergeSessionStatus::ResourceLimit; result.message = QString::fromUtf8(error.what());
    } catch (const std::exception& error) {
        result.status = MergeSessionStatus::Error; result.message = QString::fromUtf8(error.what());
    }
    if (result.status != MergeSessionStatus::Ready && lineNumber >= 0)
        result.message.prepend(QStringLiteral("RESULT line %1: ").arg(lineNumber + 1));
    return result;
}
} // namespace diffmerge::gui
