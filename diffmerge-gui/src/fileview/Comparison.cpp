#include <diffmerge/Comparison.h>
#include <diffcore/DiffEngine.h>
#include <climits>
#include <stdexcept>

namespace diffmerge::gui {

TextSnapshot TextSnapshot::fromText(const QString& text, const QString& label, const QString& fileName) {
    if (text.size() > INT_MAX - 3) throw std::length_error("Text exceeds integer coordinates");
    TextSnapshot snapshot;
    snapshot.label = label;
    snapshot.fileName = fileName;
    snapshot.finalNewline = false;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != '\r' && text[i] != '\n') continue;
        snapshot.lines.append(text.mid(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            snapshot.lineEndings.append(LineEnding::CRLF);
            ++i;
        } else snapshot.lineEndings.append(text[i] == '\r' ? LineEnding::CR : LineEnding::LF);
        start = i + 1;
    }
    if (start < text.size()) {
        snapshot.lines.append(text.mid(start));
        snapshot.lineEndings.append(LineEnding::None);
    } else if (!text.isEmpty()) snapshot.finalNewline = true;
    return snapshot;
}

namespace {
void validate(const TextSnapshot& snapshot, const PreparationLimits& limits,
              std::uint64_t& lines, std::uint64_t& units, diffcore::ComputationControl& control) {
    control.step();
    lines += snapshot.lines.size();
    if (lines > limits.maxInputLines || lines > INT_MAX - 3)
        throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
    if (snapshot.finalNewline.value_or(false) && snapshot.lines.isEmpty())
        throw std::invalid_argument("An empty file cannot have a final newline");
    if (!snapshot.lineEndings.isEmpty() && snapshot.lineEndings.size() != snapshot.lines.size())
        throw std::invalid_argument("Line ending metadata must have one entry per line");
    for (int i = 0; i < snapshot.lines.size(); ++i) {
        const auto& line = snapshot.lines[i];
        control.step();
        if (std::uint64_t(line.size()) > limits.maxLineCodeUnits)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        units += std::uint64_t(line.size()) + 1;
        if (units > limits.maxInputCodeUnits || units > INT_MAX - 3)
            throw diffcore::ComputationStopped(diffcore::StopReason::ResourceLimit);
        for (QChar c : line) {
            control.step();
            if (c == '\r' || c == '\n') throw std::invalid_argument("Snapshot lines must omit newline delimiters");
        }
        if (snapshot.lineEndings.isEmpty()) continue;
        const auto ending = snapshot.lineEndings[i];
        if (ending < LineEnding::None || ending > LineEnding::Unknown)
            throw std::invalid_argument("Invalid line ending metadata");
        if (i + 1 < snapshot.lines.size() && ending == LineEnding::None)
            throw std::invalid_argument("A non-final line must have a terminator");
        if (i + 1 == snapshot.lines.size() && snapshot.finalNewline && ending != LineEnding::Unknown &&
            (*snapshot.finalNewline == (ending == LineEnding::None)))
            throw std::invalid_argument("Final-newline and line-ending metadata disagree");
    }
}
} // namespace

PrepareResult prepareComparison(const TextSnapshot& left, const TextSnapshot& right,
    const ComparisonOptions& options, const diffcore::CancellationToken& cancellation) {
    const auto start = std::chrono::steady_clock::now();
    PrepareResult result;
    diffcore::ComputationControl control(cancellation, options.limits.maxWork, options.limits.maxTraceEntries);
    try {
        std::uint64_t lines = 0, units = 0;
        validate(left, options.limits, lines, units, control);
        validate(right, options.limits, lines, units, control);
        auto prepared = std::shared_ptr<PreparedComparison>(new PreparedComparison);
        prepared->m_left = left;
        prepared->m_right = right;
        prepared->m_options = options;
        diffcore::DiffEngine engine;
        prepared->m_diff = engine.compute(left.lines, right.lines, options.diff, &control);
        if (options.splitReplacementsRightFirst) {
            if (!options.diff.mergeReplaceHunks)
                throw std::invalid_argument("Right-first replacement layout requires merged replacement hunks");
            std::vector<diffcore::Hunk> ordered;
            ordered.reserve(prepared->m_diff.hunks.size());
            for (const auto& h:prepared->m_diff.hunks) {
                control.step();
                if (h.type!=diffcore::ChangeType::Replace) { ordered.push_back(h); continue; }
                ordered.push_back({diffcore::ChangeType::Insert,{h.leftRange.start,0},h.rightRange});
                ordered.push_back({diffcore::ChangeType::Delete,h.leftRange,{h.rightRange.end(),0}});
                --prepared->m_diff.stats.modifications;
            }
            prepared->m_diff.hunks=std::move(ordered);
        }
        prepared->m_model.build(prepared->m_diff, left.lines, right.lines, &control);
        prepared->m_highlights = IntraLineDiffEngine::compute(prepared->m_diff, left.lines, right.lines, &control, options.highlightDetail);
        prepared->m_mapping.build(prepared->m_diff, &control);
        control.step(0); // Cancellation after the last expensive phase still discards the result.
        result.comparison = std::move(prepared);
        result.status = PreparationStatus::Ready;
    } catch (const diffcore::ComputationStopped& stopped) {
        result.status = stopped.reason == diffcore::StopReason::Cancelled
            ? PreparationStatus::Cancelled : PreparationStatus::ResourceLimit;
        result.message = QString::fromUtf8(stopped.what());
    } catch (const std::bad_alloc&) {
        result.status = PreparationStatus::ResourceLimit;
        result.message = QStringLiteral("Comparison allocation failed");
    } catch (const std::length_error& error) {
        result.status = PreparationStatus::ResourceLimit;
        result.message = QString::fromUtf8(error.what());
    } catch (const std::exception& error) {
        result.status = PreparationStatus::Error;
        result.message = QString::fromUtf8(error.what());
    }
    result.preparationTime = std::chrono::steady_clock::now() - start;
    result.workPerformed = control.workPerformed();
    return result;
}
} // namespace diffmerge::gui
