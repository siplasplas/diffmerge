#include "DiffHighlighter.h"
#include <algorithm>

namespace diffmerge::gui {

void DiffHighlighter::setSyntax(std::unique_ptr<qce::IHighlighter> syntax) {
    m_syntax = std::move(syntax);
    rebuildAttributes();
}

void DiffHighlighter::setData(const QVector<QVector<CharRange>>& ranges, const QColor& background) {
    m_ranges = ranges;
    m_strongBg = background;
    rebuildAttributes();
}

void DiffHighlighter::rebuildAttributes() {
    m_attrs = m_syntax ? m_syntax->attributes() : QVector<qce::TextAttribute>{};
    // Syntax backgrounds must not hide block colors, even on equal lines.
    for (auto& attribute : m_attrs) attribute.background = QColor{};
    m_syntaxAttributeCount = m_attrs.size();
    qce::TextAttribute changed;
    changed.background = m_strongBg;
    m_attrs.append(changed);
    for (int i = 0; i < m_syntaxAttributeCount; ++i) {
        auto attribute = m_attrs[i];
        attribute.background = m_strongBg;
        m_attrs.append(attribute);
    }
}

qce::HighlightState DiffHighlighter::initialState() const {
    auto state = m_syntax ? m_syntax->initialState() : qce::HighlightState{};
    state.contextStack.append(0);
    state.captureStack.append(QStringList{});
    return state;
}

void DiffHighlighter::highlightLine(const QString& line, const qce::HighlightState& stateIn,
    QVector<qce::StyleSpan>& spans, qce::HighlightState& stateOut) const {
    auto syntaxState = stateIn;
    const int lineNumber = syntaxState.contextStack.isEmpty() ? 0 : syntaxState.contextStack.takeLast();
    if (!syntaxState.captureStack.isEmpty()) syntaxState.captureStack.removeLast();
    QVector<qce::StyleSpan> syntaxSpans;
    stateOut = {};
    if (m_syntax) m_syntax->highlightLine(line, syntaxState, syntaxSpans, stateOut);
    stateOut.contextStack.append(lineNumber + 1);
    stateOut.captureStack.append(QStringList{});
    spans.clear();

    static const QVector<CharRange> empty;
    const auto& changes = lineNumber >= 0 && lineNumber < m_ranges.size() ? m_ranges[lineNumber] : empty;
    QVector<int> boundaries{0, int(line.size())};
    const auto addBoundary = [&](int start, int length) {
        boundaries.append(std::clamp(start, 0, int(line.size())));
        boundaries.append(int(std::clamp(qint64(start) + length, qint64(0), qint64(line.size()))));
    };
    for (const auto& span : syntaxSpans) addBoundary(span.start, span.length);
    for (const auto& range : changes) addBoundary(range.start, range.length);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    int syntaxIndex = 0, changeIndex = 0;
    for (int i = 0; i + 1 < boundaries.size(); ++i) {
        const int start = boundaries[i], length = boundaries[i + 1] - start;
        while (syntaxIndex < syntaxSpans.size() && syntaxSpans[syntaxIndex].start + syntaxSpans[syntaxIndex].length <= start) ++syntaxIndex;
        while (changeIndex < changes.size() && changes[changeIndex].start + changes[changeIndex].length <= start) ++changeIndex;
        int attribute = -1;
        if (syntaxIndex < syntaxSpans.size() && syntaxSpans[syntaxIndex].start <= start)
            attribute = syntaxSpans[syntaxIndex].attributeId;
        if (attribute < 0 || attribute >= m_syntaxAttributeCount) attribute = -1;
        const bool changed = changeIndex < changes.size() && changes[changeIndex].start <= start;
        if (changed) attribute = attribute < 0 ? m_syntaxAttributeCount : m_syntaxAttributeCount + 1 + attribute;
        if (attribute < 0 || length == 0) continue;
        if (!spans.isEmpty() && spans.last().attributeId == attribute && spans.last().start + spans.last().length == start)
            spans.last().length += length;
        else spans.append({start, length, attribute});
    }
}

const QVector<qce::TextAttribute>& DiffHighlighter::attributes() const { return m_attrs; }
} // namespace diffmerge::gui
