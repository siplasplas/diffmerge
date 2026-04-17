#include "CliOptions.h"

namespace diffmerge::cli {

namespace {

// Recognize a short flag like "-i" or a bundle like "-iw".
bool isShortFlagBundle(const QString& arg) {
    return arg.size() >= 2 && arg.startsWith(QLatin1Char('-'))
        && arg.at(1) != QLatin1Char('-');
}

// Apply a single short flag character to the options.
// Returns false if the flag is unknown.
bool applyShortFlag(QChar flag, CliOptions& opts) {
    switch (flag.unicode()) {
        case 'i': opts.diffOpts.ignoreCase = true; return true;
        case 'w': opts.diffOpts.ignoreWhitespace = true; return true;
        case 'b': opts.diffOpts.ignoreTrailingWhitespace = true; return true;
        case 'h': opts.showHelp = true; return true;
        default:  return false;
    }
}

// Returns true if arg was a recognized long option, false if it's something
// else (e.g. a filename or an unknown option).
// Sets parseError on invalid long options.
bool applyLongOption(const QString& arg, CliOptions& opts) {
    if (arg == QStringLiteral("--help")) {
        opts.showHelp = true;
        return true;
    }
    if (arg == QStringLiteral("--brief")) {
        opts.brief = true;
        return true;
    }
    if (arg == QStringLiteral("--color")) {
        opts.colorExplicit = true;
        opts.colorForced = true;
        return true;
    }
    if (arg == QStringLiteral("--no-color")) {
        opts.colorExplicit = true;
        opts.colorForced = false;
        return true;
    }
    return false;
}

// Parse a short flag bundle like "-iwb". Sets parseError on unknown flags.
void parseShortBundle(const QString& arg, CliOptions& opts) {
    for (int i = 1; i < arg.size(); ++i) {
        if (!applyShortFlag(arg.at(i), opts)) {
            opts.parseError = true;
            opts.errorMessage = QStringLiteral("unknown option: -%1")
                                    .arg(arg.at(i));
            return;
        }
    }
}

// Accept a positional argument (file path); sets parseError if we already
// have both.
void acceptPositional(const QString& arg, CliOptions& opts) {
    if (opts.leftPath.isEmpty()) {
        opts.leftPath = arg;
    } else if (opts.rightPath.isEmpty()) {
        opts.rightPath = arg;
    } else {
        opts.parseError = true;
        opts.errorMessage = QStringLiteral("extra argument: %1").arg(arg);
    }
}

}  // namespace

CliOptions parseArgs(const QStringList& args) {
    CliOptions opts;
    // args[0] is the program name - skip it.
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        if (opts.parseError || opts.showHelp) break;

        if (arg.startsWith(QStringLiteral("--"))) {
            if (!applyLongOption(arg, opts)) {
                opts.parseError = true;
                opts.errorMessage = QStringLiteral("unknown option: %1")
                                        .arg(arg);
            }
        } else if (isShortFlagBundle(arg)) {
            parseShortBundle(arg, opts);
        } else {
            acceptPositional(arg, opts);
        }
    }

    if (!opts.showHelp && !opts.parseError) {
        if (opts.leftPath.isEmpty() || opts.rightPath.isEmpty()) {
            opts.parseError = true;
            opts.errorMessage = QStringLiteral(
                "missing file argument(s); use -h for help");
        }
    }
    return opts;
}

QString helpText(const QString& programName) {
    return QStringLiteral(
        "Usage: %1 [OPTIONS] FILE_A FILE_B\n"
        "\n"
        "Compare two text files and print their differences.\n"
        "\n"
        "Options:\n"
        "  -i            Ignore case\n"
        "  -w            Ignore all whitespace (collapse runs, trim ends)\n"
        "  -b            Ignore trailing whitespace only\n"
        "  --brief       Only report whether the files differ\n"
        "  --color       Force ANSI color output\n"
        "  --no-color    Disable ANSI color output\n"
        "  -h, --help    Show this help and exit\n"
        "\n"
        "Exit codes:\n"
        "  0 - files identical\n"
        "  1 - files differ\n"
        "  2 - error\n"
    ).arg(programName);
}

}  // namespace diffmerge::cli
