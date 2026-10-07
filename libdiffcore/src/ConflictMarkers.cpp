#include <diffcore/ConflictMarkers.h>
#include <QCryptographicHash>
#include <QStringDecoder>
#include <stdexcept>

namespace diffcore {
namespace detail {
ParseConflictResult parseConflictFileControlled(const QByteArray& input, const MarkerOptions& options,
    const ConflictLimits& limits, ComputationControl& control) {
    ParseConflictResult result;
    int line = 0;
    qint64 offset = input.startsWith(QByteArray::fromHex("efbbbf")) ? 3 : 0;
    const auto limit = [] { throw ComputationStopped(StopReason::ResourceLimit); };
    try {
        control.step(0);
        if (options.markerSize < 1 || quint64(options.markerSize) > limits.maxLineUnits)
            throw std::invalid_argument("Marker size must be positive and within the line limit");
        if (quint64(input.size()) > limits.maxInputBytes || quint64(options.literalMarkerLines.size()) > limits.maxLines) limit();
        // Bound decoding chunks and reject invalid UTF-8 rather than replacing bytes.
        QStringDecoder decoder(QStringDecoder::Utf8);
        quint64 units = 0;
        for (qint64 p = offset; p < input.size(); p += 4096) {
            const auto chunk = QByteArrayView(input).sliced(p, std::min<qint64>(4096, input.size() - p));
            control.step(chunk.size());
            if (chunk.contains('\0')) { result.status = ConflictStatus::Unsupported; result.message = "Binary input is not supported"; return result; }
            const QString decoded = decoder(chunk);
            units += decoded.size();
            if (units > limits.maxCodeUnits) limit();
            if (decoder.hasError()) { result.status = ConflictStatus::Unsupported; result.message = "Input is not valid UTF-8"; return result; }
        }
        // A stateless pass detects incomplete final sequences too.
        QStringDecoder finalDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString finalText = finalDecoder(QByteArrayView(input).sliced(offset));
        Q_UNUSED(finalText);
        if (finalDecoder.hasError()) { result.status = ConflictStatus::Unsupported; result.message = "Input is not valid UTF-8"; return result; }
        enum class State { Outside, Left, Base, Right };
        State state = State::Outside;
        QVector<ConflictBlock> blocks;
        ConflictBlock current;
        qint64 fragmentStart = 0;
        quint64 metadata = 0;
        for (; offset < input.size(); ++line) {
            if (quint64(line) >= limits.maxLines) limit();
            const qint64 start = offset;
            while (offset < input.size() && input[offset] != '\r' && input[offset] != '\n') { control.step(); ++offset; }
            const auto content = input.mid(start, offset - start);
            if (quint64(QString::fromUtf8(content).size()) > limits.maxLineUnits) limit();
            if (offset < input.size()) {
                const auto eol = input[offset++];
                if (eol == '\r' && offset < input.size() && input[offset] == '\n') ++offset;
            }
            if (options.literalMarkerLines.contains(line) || content.isEmpty()) continue;
            const char first = content[0];
            if (first != '<' && first != '|' && first != '=' && first != '>') continue;
            int count = 0;
            while (count < content.size() && content[count] == first) { control.step(); ++count; }
            if (count < options.markerSize) continue;
            if (count != options.markerSize || (count < content.size() && content[count] != ' ' && content[count] != '\t'))
                throw std::invalid_argument("Malformed marker length or label delimiter");
            if (first == '=' && count != content.size()) throw std::invalid_argument("A separator marker cannot have a label");
            const auto label = QString::fromUtf8(content.mid(count)).trimmed();
            metadata += label.size() * 2;
            if (metadata > limits.maxMetadataBytes) limit();
            if (state == State::Outside) {
                if (first != '<') throw std::invalid_argument("Marker outside a conflict block");
                if (quint64(blocks.size()) >= limits.maxConflicts) limit();
                current = {}; current.envelope.start = start; current.lines.start = line;
                current.leftLabel = label; fragmentStart = offset; state = State::Left;
            } else if (state == State::Left && first == '|') {
                current.left = {fragmentStart, start - fragmentStart}; current.baseLabel = label;
                fragmentStart = offset; state = State::Base;
            } else if ((state == State::Left || state == State::Base) && first == '=') {
                if (state == State::Left) current.left = {fragmentStart, start - fragmentStart};
                else current.base = ByteRange{fragmentStart, start - fragmentStart};
                fragmentStart = offset; state = State::Right;
            } else if (state == State::Right && first == '>') {
                current.right = {fragmentStart, start - fragmentStart}; current.rightLabel = label;
                current.envelope.length = offset - current.envelope.start;
                current.lines.count = line + 1 - current.lines.start;
                blocks.append(current); state = State::Outside;
            } else throw std::invalid_argument("Nested, repeated or out-of-order conflict marker");
        }
        for (int literal : options.literalMarkerLines) {
            control.step();
            if (literal < 0 || literal >= line) throw std::invalid_argument("Literal marker line is outside input");
        }
        if (state != State::Outside) throw std::invalid_argument("Incomplete conflict block at end of input");
        control.step(input.size());
        result.file = {input, QCryptographicHash::hash(input, QCryptographicHash::Sha256), options, blocks};
        for (auto& block : result.file.conflicts) {
            const auto key = result.file.sha256.toHex() + ':' + QByteArray::number(block.envelope.start) + ':'
                + QByteArray::number(block.envelope.length) + ':' + QByteArray::number(options.markerSize);
            block.id = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
        }
        result.status = ConflictStatus::Complete;
    } catch (const ComputationStopped& e) {
        result.status = e.reason == StopReason::Cancelled ? ConflictStatus::Cancelled : ConflictStatus::ResourceLimit;
        result.message = QString::fromUtf8(e.what());
    } catch (const std::bad_alloc&) {
        result.status = ConflictStatus::ResourceLimit; result.message = "Marker parser allocation failed";
    } catch (const std::exception& e) {
        result.status = ConflictStatus::InvalidInput; result.message = QString::fromUtf8(e.what());
    }
    if (result.status != ConflictStatus::Complete) {
        result.file = {}; result.errorLine = line; result.errorByteOffset = offset;
        result.message.prepend(QStringLiteral("Input line %1: ").arg(line + 1));
    }
    result.workPerformed = control.workPerformed();
    return result;
}
}
ParseConflictResult parseConflictFile(const QByteArray& bytes, const MarkerOptions& options,
    const ConflictLimits& limits, const CancellationToken& cancellation) {
    ComputationControl control(cancellation, limits.maxWork, limits.maxTraceEntries);
    return detail::parseConflictFileControlled(bytes, options, limits, control);
}
} // namespace diffcore
