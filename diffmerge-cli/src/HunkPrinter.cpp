#include "HunkPrinter.h"

namespace diffmerge::cli {

namespace {

// ANSI color codes - used only when opts.useColor == true.
constexpr const char* kAnsiRed    = "\x1b[31m";
constexpr const char* kAnsiGreen  = "\x1b[32m";
constexpr const char* kAnsiCyan   = "\x1b[36m";
constexpr const char* kAnsiReset  = "\x1b[0m";

// Convert a LineRange (0-based) to the "L,C" form used in diff headers
// (1-based, with count=0 rendered as start line 0 to match GNU diff).
QString rangeToHeader(const diffcore::LineRange& r) {
    if (r.count == 0) {
        // Empty range: GNU diff prints the line just before the insertion,
        // or 0 if at start. Simplified: use 0-count form.
        return QStringLiteral("%1,0").arg(r.start);
    }
    return QStringLiteral("%1,%2").arg(r.start + 1).arg(r.count);
}

}  // namespace

HunkPrinter::HunkPrinter(QTextStream& out,
                         const QStringList& leftLines,
                         const QStringList& rightLines,
                         const PrintOptions& opts)
    : m_out(out), m_left(leftLines), m_right(rightLines), m_opts(opts) {}

QString HunkPrinter::colored(const QString& text, const char* ansiCode) const {
    if (!m_opts.useColor) return text;
    return QLatin1String(ansiCode) + text + QLatin1String(kAnsiReset);
}

void HunkPrinter::printHeader(const diffcore::Hunk& h) {
    const QString header = QStringLiteral("@@ -%1 +%2 @@")
        .arg(rangeToHeader(h.leftRange), rangeToHeader(h.rightRange));
    m_out << colored(header, kAnsiCyan) << '\n';
}

void HunkPrinter::printDeletedLines(const diffcore::LineRange& r) {
    for (int i = 0; i < r.count; ++i) {
        const int idx = r.start + i;
        if (idx < 0 || idx >= m_left.size()) continue;  // Defensive
        m_out << colored(QStringLiteral("- %1").arg(m_left[idx]), kAnsiRed)
              << '\n';
    }
}

void HunkPrinter::printInsertedLines(const diffcore::LineRange& r) {
    for (int i = 0; i < r.count; ++i) {
        const int idx = r.start + i;
        if (idx < 0 || idx >= m_right.size()) continue;  // Defensive
        m_out << colored(QStringLiteral("+ %1").arg(m_right[idx]), kAnsiGreen)
              << '\n';
    }
}

void HunkPrinter::printReplace(const diffcore::Hunk& h) {
    // Same visual treatment as Delete followed by Insert.
    printDeletedLines(h.leftRange);
    printInsertedLines(h.rightRange);
}

void HunkPrinter::printHunk(const diffcore::Hunk& h) {
    printHeader(h);
    switch (h.type) {
        case diffcore::ChangeType::Delete:
            printDeletedLines(h.leftRange);
            break;
        case diffcore::ChangeType::Insert:
            printInsertedLines(h.rightRange);
            break;
        case diffcore::ChangeType::Replace:
            printReplace(h);
            break;
        case diffcore::ChangeType::Equal:
            // Not printed in default output (contextLines handled separately
            // in a future version).
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
