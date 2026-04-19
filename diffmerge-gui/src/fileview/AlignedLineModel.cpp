#include "AlignedLineModel.h"

#include <QStringList>
#include <algorithm>
#include <cassert>

namespace diffmerge::gui {

namespace {

// Append a single row to the side's aligned vectors.
// fileLine == -1 means placeholder (text is empty).
void pushRow(std::vector<AlignedRow>& rows, QStringList& text,
             int fileLine, diffcore::ChangeType ct, const QString& content) {
    AlignedRow r;
    r.fileLineNumber = fileLine;
    r.changeType = ct;
    rows.push_back(r);
    text.append(content);
}

}  // namespace

void AlignedLineModel::appendEqual(const diffcore::Hunk& h,
                                   const QStringList& leftLines,
                                   const QStringList& rightLines) {
    // Equal ranges have identical counts on both sides; iterate once.
    const int count = h.leftRange.count;
    assert(h.rightRange.count == count);
    for (int i = 0; i < count; ++i) {
        const int li = h.leftRange.start + i;
        const int ri = h.rightRange.start + i;
        pushRow(m_leftRows, m_leftText,
                li, diffcore::ChangeType::Equal, leftLines.at(li));
        pushRow(m_rightRows, m_rightText,
                ri, diffcore::ChangeType::Equal, rightLines.at(ri));
    }
}

void AlignedLineModel::appendInsert(const diffcore::Hunk& h,
                                    const QStringList& rightLines) {
    // Right side has content; left side gets placeholders of the same count.
    for (int i = 0; i < h.rightRange.count; ++i) {
        const int ri = h.rightRange.start + i;
        pushRow(m_leftRows, m_leftText,
                -1, diffcore::ChangeType::Insert, QString());
        pushRow(m_rightRows, m_rightText,
                ri, diffcore::ChangeType::Insert, rightLines.at(ri));
    }
}

void AlignedLineModel::appendDelete(const diffcore::Hunk& h,
                                    const QStringList& leftLines) {
    // Left side has content; right side gets placeholders of the same count.
    for (int i = 0; i < h.leftRange.count; ++i) {
        const int li = h.leftRange.start + i;
        pushRow(m_leftRows, m_leftText,
                li, diffcore::ChangeType::Delete, leftLines.at(li));
        pushRow(m_rightRows, m_rightText,
                -1, diffcore::ChangeType::Delete, QString());
    }
}

void AlignedLineModel::appendReplace(const diffcore::Hunk& h,
                                     const QStringList& leftLines,
                                     const QStringList& rightLines) {
    // A Replace hunk can have different counts on each side. Pair them up
    // line by line; pad the shorter side with placeholders.
    const int maxCount = std::max(h.leftRange.count, h.rightRange.count);
    for (int i = 0; i < maxCount; ++i) {
        const bool hasLeft = i < h.leftRange.count;
        const bool hasRight = i < h.rightRange.count;
        const int li = hasLeft ? h.leftRange.start + i : -1;
        const int ri = hasRight ? h.rightRange.start + i : -1;
        pushRow(m_leftRows, m_leftText,
                li, diffcore::ChangeType::Replace,
                hasLeft ? leftLines.at(li) : QString());
        pushRow(m_rightRows, m_rightText,
                ri, diffcore::ChangeType::Replace,
                hasRight ? rightLines.at(ri) : QString());
    }
}

void AlignedLineModel::build(const diffcore::DiffResult& diff,
                             const QStringList& leftLines,
                             const QStringList& rightLines) {
    m_leftRows.clear();
    m_rightRows.clear();
    m_leftText.clear();
    m_rightText.clear();
    m_hunkAlignedStarts.clear();
    m_hunkAlignedEnds.clear();

    for (const diffcore::Hunk& h : diff.hunks) {
        const bool isChange = h.type != diffcore::ChangeType::Equal;
        if (isChange)
            m_hunkAlignedStarts.append(static_cast<int>(m_leftRows.size()));
        switch (h.type) {
            case diffcore::ChangeType::Equal:
                appendEqual(h, leftLines, rightLines);
                break;
            case diffcore::ChangeType::Insert:
                appendInsert(h, rightLines);
                break;
            case diffcore::ChangeType::Delete:
                appendDelete(h, leftLines);
                break;
            case diffcore::ChangeType::Replace:
                appendReplace(h, leftLines, rightLines);
                break;
        }
        if (isChange)
            m_hunkAlignedEnds.append(static_cast<int>(m_leftRows.size()));
    }

    // Post-condition: both sides have the same number of aligned rows.
    assert(m_leftRows.size() == m_rightRows.size());
}

const AlignedRow& AlignedLineModel::row(Side side, int alignedRow) const {
    const auto& rows = (side == Side::Left) ? m_leftRows : m_rightRows;
    return rows.at(static_cast<size_t>(alignedRow));
}

QString AlignedLineModel::text(Side side, int alignedRow) const {
    const auto& t = (side == Side::Left) ? m_leftText : m_rightText;
    return t.at(alignedRow);
}

QString AlignedLineModel::buildDocumentText(Side side) const {
    const auto& t = (side == Side::Left) ? m_leftText : m_rightText;
    return t.join(QLatin1Char('\n'));
}

QStringList AlignedLineModel::documentLines(Side side) const {
    const auto& rows = (side == Side::Left) ? m_leftRows : m_rightRows;
    const auto& text = (side == Side::Left) ? m_leftText : m_rightText;
    QStringList result;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (!rows[i].isPlaceholder())
            result.append(text[i]);
    }
    return result;
}

QVector<AlignedLineModel::FillerInfo> AlignedLineModel::fillerRanges(Side side) const {
    const auto& rows = (side == Side::Left) ? m_leftRows : m_rightRows;
    QVector<FillerInfo> result;
    int docLine = 0;
    int i = 0;
    const int total = static_cast<int>(rows.size());
    while (i < total) {
        if (!rows[i].isPlaceholder()) {
            ++docLine;
            ++i;
        } else {
            const diffcore::ChangeType ct = rows[i].changeType;
            int count = 0;
            while (i < total && rows[i].isPlaceholder()) {
                ++count;
                ++i;
            }
            result.append({docLine, count, ct});
        }
    }
    return result;
}

diffcore::ChangeType AlignedLineModel::docLineChangeType(Side side, int docLine) const {
    const auto& rows = (side == Side::Left) ? m_leftRows : m_rightRows;
    int dc = 0;
    for (const auto& row : rows) {
        if (!row.isPlaceholder()) {
            if (dc == docLine) return row.changeType;
            ++dc;
        }
    }
    return diffcore::ChangeType::Equal;
}

int AlignedLineModel::docLineBeforeAligned(Side side, int alignedRow) const {
    const auto& rows = (side == Side::Left) ? m_leftRows : m_rightRows;
    int dc = 0;
    for (int i = 0; i < alignedRow && i < static_cast<int>(rows.size()); ++i) {
        if (!rows[i].isPlaceholder()) ++dc;
    }
    return dc;
}

}  // namespace diffmerge::gui
