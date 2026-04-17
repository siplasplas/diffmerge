// Prints a DiffResult to stdout in a human-readable format.
// Not unified-diff compatible (that comes later in the full CLI);
// format is:
//
//   @@ -L,C +L,C @@           (range header, 1-based)
//   - deleted line            (one per deleted line)
//   + inserted line           (one per inserted line)
//   ~ ... replace block starts with --- then +++
//
// Equal hunks are skipped in the default output; pass --verbose
// to show a few context lines around each change.

#ifndef DIFFMERGE_CLI_HUNKPRINTER_H
#define DIFFMERGE_CLI_HUNKPRINTER_H

#include <QStringList>
#include <QTextStream>

#include <diffcore/DiffTypes.h>

namespace diffmerge::cli {

struct PrintOptions {
    bool useColor = false;   // ANSI colors (auto-detected from isatty)
    int contextLines = 0;    // Equal lines shown around each hunk (0 = none)
};

class HunkPrinter {
public:
    HunkPrinter(QTextStream& out,
                const QStringList& leftLines,
                const QStringList& rightLines,
                const PrintOptions& opts);

    void printAll(const diffcore::DiffResult& result);

private:
    // Emit one hunk (non-Equal). Helpers below keep this short.
    void printHunk(const diffcore::Hunk& h);

    // Emit a range header "@@ -L,C +L,C @@" with 1-based line numbers.
    void printHeader(const diffcore::Hunk& h);

    // Emit deleted or inserted lines with a single-char prefix.
    void printDeletedLines(const diffcore::LineRange& r);
    void printInsertedLines(const diffcore::LineRange& r);

    // Emit a Replace hunk: deleted lines first, then inserted lines.
    void printReplace(const diffcore::Hunk& h);

    // Wrap text in ANSI escape if colors are on; otherwise pass-through.
    QString colored(const QString& text, const char* ansiCode) const;

    QTextStream& m_out;
    const QStringList& m_left;
    const QStringList& m_right;
    PrintOptions m_opts;
};

}  // namespace diffmerge::cli

#endif  // DIFFMERGE_CLI_HUNKPRINTER_H
