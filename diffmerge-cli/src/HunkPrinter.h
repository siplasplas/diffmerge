// Prints a DiffResult to stdout in the traditional diff normal format:
//
//   LcR           (Replace: left range, 'c', right range)
//   < deleted     (lines from left file, prefixed "< ")
//   ---
//   > inserted    (lines from right file, prefixed "> ")
//
//   LdR           (Delete)
//   < deleted line(s)
//
//   LaR           (Insert / append)
//   > inserted line(s)
//
// Range notation: single line → "N"; multiple lines → "N,M" (both 1-based).
// Equal hunks are never printed.

#ifndef DIFFMERGE_CLI_HUNKPRINTER_H
#define DIFFMERGE_CLI_HUNKPRINTER_H

#include <QStringList>
#include <QTextStream>

#include <diffcore/DiffTypes.h>

namespace diffmerge::cli {

struct PrintOptions {
    bool useColor = false;
};

class HunkPrinter {
public:
    HunkPrinter(QTextStream& out,
                const QStringList& leftLines,
                const QStringList& rightLines,
                const PrintOptions& opts);

    void printAll(const diffcore::DiffResult& result);

private:
    void printHunk(const diffcore::Hunk& h);
    void printDeletedLines(const diffcore::LineRange& r);
    void printInsertedLines(const diffcore::LineRange& r);

    // "N" for a single-line range, "N,M" for multi-line (1-based).
    static QString formatAddr(int start, int count);
    // Build the "LaR", "LdR", or "LcR" command line.
    static QString hunkHeader(const diffcore::Hunk& h);

    QString colored(const QString& text, const char* ansiCode) const;

    QTextStream& m_out;
    const QStringList& m_left;
    const QStringList& m_right;
    PrintOptions m_opts;
};

}  // namespace diffmerge::cli

#endif  // DIFFMERGE_CLI_HUNKPRINTER_H
