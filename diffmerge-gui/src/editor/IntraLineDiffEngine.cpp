#include <diffmerge/IntraLineDiffEngine.h>

#include <QStringView>
#include <algorithm>
#include <vector>

#include <diffcore/SequenceDiff.h>

namespace diffmerge::gui {
namespace {

using Range = IntraLineDiffEngine::CharRange;
enum class TokenKind { Word, Spacing, Punctuation };
struct Token {
    QStringView text;
    int start;
    TokenKind kind;
    bool operator==(const Token& other) const { return text == other.text; }
};

TokenKind kindOf(QChar c) {
    if (c.isSpace()) return TokenKind::Spacing;
    if (c.isLetterOrNumber() || c == '_' ||
        c.category() == QChar::Mark_NonSpacing ||
        c.category() == QChar::Mark_SpacingCombining ||
        c.category() == QChar::Mark_Enclosing) return TokenKind::Word;
    return TokenKind::Punctuation;
}

std::vector<Token> tokenize(const QString& text, diffcore::ComputationControl* control) {
    std::vector<Token> tokens;
    for (int start = 0; start < text.size();) {
        diffcore::checkpoint(control);
        const auto kind = kindOf(text[start]);
        int end = start + 1;
        if (kind != TokenKind::Punctuation) {
            while (end < text.size() && kindOf(text[end]) == kind) { diffcore::checkpoint(control); ++end; }
        } else if (text[start].isHighSurrogate() && end < text.size() &&
                   text[end].isLowSurrogate()) {
            ++end; // Keep supplementary characters together as one token.
        }
        tokens.push_back({QStringView(text).mid(start, end - start), start, kind});
        start = end;
    }
    return tokens;
}

void appendRange(QVector<Range>& ranges, int start, int length) {
    if (!length) return;
    if (!ranges.isEmpty() && ranges.last().start + ranges.last().length == start)
        ranges.last().length += length;
    else ranges.append({start, length});
}

Range tokenRange(const std::vector<Token>& tokens, const diffcore::LineRange& range) {
    if (!range.count) return {0, 0};
    const int start = tokens[range.start].start;
    const auto& last = tokens[range.end() - 1];
    return {start, last.start + int(last.text.size()) - start};
}

QString withoutSpacing(const QString& text, diffcore::ComputationControl* control) {
    QString key;
    key.reserve(text.size());
    for (QChar c : text) { diffcore::checkpoint(control); if (!c.isSpace()) key.append(c); }
    return key;
}

void diffCharacters(const QString& left, const QString& right, int leftOffset, int rightOffset,
                    QVector<Range>& leftRanges, QVector<Range>& rightRanges, diffcore::ComputationControl* control) {
    // QString elements are UTF-16 units, matching the editor's column offsets.
    const auto result = diffcore::SequenceDiff::compute(left, right, {}, control);
    for (const auto& h : result.hunks) {
        diffcore::checkpoint(control);
        if (h.type == diffcore::ChangeType::Equal) continue;
        appendRange(leftRanges, leftOffset + h.leftRange.start, h.leftRange.count);
        appendRange(rightRanges, rightOffset + h.rightRange.start, h.rightRange.count);
    }
}

// Translate joined-block offsets back into original document line/column ranges.
void projectRanges(const QStringList& lines, const diffcore::LineRange& block,
                   const QVector<Range>& ranges, QVector<QVector<Range>>& output, diffcore::ComputationControl* control) {
    int offset = 0, rangeIndex = 0;
    for (int line = block.start; line < block.end(); ++line) {
        diffcore::checkpoint(control);
        const int end = offset + lines[line].size();
        while (rangeIndex < ranges.size() &&
               ranges[rangeIndex].start + ranges[rangeIndex].length <= offset) ++rangeIndex;
        for (int i = rangeIndex; i < ranges.size() && ranges[i].start < end; ++i) {
            diffcore::checkpoint(control);
            const int start = std::max(offset, ranges[i].start);
            const int finish = std::min(end, ranges[i].start + ranges[i].length);
            appendRange(output[line], start - offset, finish - start);
        }
        offset = end + 1; // The joining newline has no drawable character cell.
    }
}

} // namespace

IntraLineDiffEngine::LinePairResult
IntraLineDiffEngine::diffText(const QString& left, const QString& right, diffcore::ComputationControl* control) {
    LinePairResult out;
    const auto leftTokens = tokenize(left, control), rightTokens = tokenize(right, control);
    const auto result = diffcore::SequenceDiff::compute(leftTokens, rightTokens, {}, control);
    for (size_t index = 0; index < result.hunks.size(); ++index) {
        diffcore::checkpoint(control);
        const auto& h = result.hunks[index];
        if (h.type == diffcore::ChangeType::Equal) continue;
        auto l = h.leftRange, r = h.rightRange;
        // Process the complete unmatched interval, including adapter splits.
        while (index + 1 < result.hunks.size() &&
               result.hunks[index + 1].type != diffcore::ChangeType::Equal) {
            ++index;
            l.count += result.hunks[index].leftRange.count;
            r.count += result.hunks[index].rightRange.count;
        }
        const auto lr = tokenRange(leftTokens, l), rr = tokenRange(rightTokens, r);
        const auto leftText = left.mid(lr.start, lr.length);
        const auto rightText = right.mid(rr.start, rr.length);
        // Splitting/joining a word through spacing still highlights only that
        // spacing. This cannot hide literal contents or changed non-space text.
        if (l.count && r.count && withoutSpacing(leftText, control) == withoutSpacing(rightText, control)) {
            diffCharacters(leftText, rightText, lr.start, rr.start, out.leftRanges, out.rightRanges, control);
            continue;
        }
        const int paired = std::min(l.count, r.count);
        for (int i = 0; i < paired; ++i) {
            diffcore::checkpoint(control);
            const auto& lt = leftTokens[l.start + i];
            const auto& rt = rightTokens[r.start + i];
            if (lt.kind == rt.kind && lt.kind != TokenKind::Punctuation) {
                diffCharacters(lt.text.toString(), rt.text.toString(), lt.start, rt.start,
                               out.leftRanges, out.rightRanges, control);
            } else {
                appendRange(out.leftRanges, lt.start, lt.text.size());
                appendRange(out.rightRanges, rt.start, rt.text.size());
            }
        }
        for (int i = paired; i < l.count; ++i) {
            diffcore::checkpoint(control);
            const auto& token = leftTokens[l.start + i];
            appendRange(out.leftRanges, token.start, token.text.size());
        }
        for (int i = paired; i < r.count; ++i) {
            diffcore::checkpoint(control);
            const auto& token = rightTokens[r.start + i];
            appendRange(out.rightRanges, token.start, token.text.size());
        }
    }
    return out;
}

IntraLineDiffEngine::Result
IntraLineDiffEngine::compute(const diffcore::DiffResult& diff,
                             const QStringList& leftLines,
                             const QStringList& rightLines, diffcore::ComputationControl* control) {
    Result out;
    out.leftRanges.resize(leftLines.size());
    out.rightRanges.resize(rightLines.size());
    for (const auto& hunk : diff.hunks) {
        diffcore::checkpoint(control);
        if (hunk.type != diffcore::ChangeType::Replace) continue;
        const auto left = leftLines.mid(hunk.leftRange.start, hunk.leftRange.count).join('\n');
        const auto right = rightLines.mid(hunk.rightRange.start, hunk.rightRange.count).join('\n');
        const auto ranges = diffText(left, right, control);
        projectRanges(leftLines, hunk.leftRange, ranges.leftRanges, out.leftRanges, control);
        projectRanges(rightLines, hunk.rightRange, ranges.rightRanges, out.rightRanges, control);
    }
    return out;
}

} // namespace diffmerge::gui
