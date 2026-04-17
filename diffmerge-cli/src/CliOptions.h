// Minimal CLI option parser for the smoke-test diffmerge tool.
// Recognizes: -i (ignore case), -w (ignore whitespace), -b (ignore trailing
// whitespace), --brief, --no-color, --color, -h/--help, and two positional
// arguments (left file, right file).

#ifndef DIFFMERGE_CLI_CLIOPTIONS_H
#define DIFFMERGE_CLI_CLIOPTIONS_H

#include <QString>
#include <QStringList>

#include <diffcore/DiffTypes.h>

namespace diffmerge::cli {

struct CliOptions {
    QString leftPath;
    QString rightPath;
    diffcore::DiffOptions diffOpts;
    bool brief = false;           // Only print "files differ" if different
    bool colorMode = false;       // Final decision after parsing + isatty
    bool colorExplicit = false;   // User passed --color or --no-color
    bool colorForced = false;     // --color (always on, even non-tty)
    bool showHelp = false;
    bool parseError = false;
    QString errorMessage;
};

// Parse argv into CliOptions. Invalid inputs set parseError=true with a
// human-readable errorMessage and showHelp=false; -h/--help sets
// showHelp=true and leaves other fields unused.
CliOptions parseArgs(const QStringList& args);

// Returns the text printed by -h/--help.
QString helpText(const QString& programName);

}  // namespace diffmerge::cli

#endif  // DIFFMERGE_CLI_CLIOPTIONS_H
