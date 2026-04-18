#include "HunkPrinter.h"

namespace diffmerge::cli {

namespace {
constexpr const char* kAnsiRed    = "\x1b[31m";
constexpr const char* kAnsiGreen  = "\x1b[32m";
constexpr const char* kAnsiCyan   = "\x1b[36m";
constexpr const char* kAnsiReset  = "\x1b[0m";
}

HunkPrinter::HunkPrinter(QTextStream& out,
                         const QStringList& leftLines,
                         const QStringList& rightLines,
                         const PrintOptions& opts)
    : m_out(out), m_left(leftLines), m_right(rightLines), m_opts(opts) {}

QString HunkPrinter::colored(const QString& text, const char* ansiCode) const {
    if (!m_opts.useColor) return text;
    return QLatin1String(ansiCode) + text + QLatin1String(kAnsiReset);
}

QString HunkPrinter::formatAddr(int start, int count) {
    if (count == 1) return QString::number(start + 1);
    return QStringLiteral("%1,%2").arg(start + 1).arg(start + count);
}

QString HunkPrinter::hunkHeader(const diffcore::Hunk& h) {
    switch (h.type) {
    case diffcore::ChangeType::Delete:
        // "L[,M]dR" — R is the right "before" line (0-based start = 1-based before).
        return formatAddr(h.leftRange.start, h.leftRange.count)
             + QStringLiteral("d")
             + QString::number(h.rightRange.start);
    case diffcore::ChangeType::Insert:
        // "LaN[,M]" — L is the left "after" line (0-based start = 1-based after).
        return QString::number(h.leftRange.start)
             + QStringLiteral("a")
             + formatAddr(h.rightRange.start, h.rightRange.count);
    case diffcore::ChangeType::Replace:
        return formatAddr(h.leftRange.start, h.leftRange.count)
             + QStringLiteral("c")
             + formatAddr(h.rightRange.start, h.rightRange.count);
    default:
        return {};
    }
}

void HunkPrinter::printDeletedLines(const diffcore::LineRange& r) {
    for (int i = 0; i < r.count; ++i) {
        const int idx = r.start + i;
        if (idx < 0 || idx >= m_left.size()) continue;
        m_out << colored(QStringLiteral("< ") + m_left[idx], kAnsiRed) << '\n';
    }
}

void HunkPrinter::printInsertedLines(const diffcore::LineRange& r) {
    for (int i = 0; i < r.count; ++i) {
        const int idx = r.start + i;
        if (idx < 0 || idx >= m_right.size()) continue;
        m_out << colored(QStringLiteral("> ") + m_right[idx], kAnsiGreen) << '\n';
    }
}

void HunkPrinter::printHunk(const diffcore::Hunk& h) {
    m_out << colored(hunkHeader(h), kAnsiCyan) << '\n';
    switch (h.type) {
    case diffcore::ChangeType::Delete:
        printDeletedLines(h.leftRange);
        break;
    case diffcore::ChangeType::Insert:
        printInsertedLines(h.rightRange);
        break;
    case diffcore::ChangeType::Replace:
        printDeletedLines(h.leftRange);
        m_out << colored(QStringLiteral("---"), kAnsiCyan) << '\n';
        printInsertedLines(h.rightRange);
        break;
    default:
        break;
    }
}

void HunkPrinter::printAll(const diffcore::DiffResult& result) {
    for (const diffcore::Hunk& h : result.hunks) {
        if (h.type == diffcore::ChangeType::Equal) continue;
        printHunk(h);
    }
}

}  // namespace diffmerge::cli
