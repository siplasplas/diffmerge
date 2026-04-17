// Smoke-test CLI for libdiffcore. Reads two files, runs the diff engine,
// prints the result. Not unified-diff compatible; full-featured CLI comes
// in a later project stage.

#include <cstdio>
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#ifndef Q_OS_WIN
#include <unistd.h>  // For isatty() on POSIX
#endif

#include <diffcore/DiffEngine.h>

#include "CliOptions.h"
#include "FileLoader.h"
#include "HunkPrinter.h"

namespace {

// Exit codes follow the GNU diff convention.
constexpr int kExitIdentical = 0;
constexpr int kExitDifferent = 1;
constexpr int kExitError     = 2;

// Decide whether to emit ANSI colors given user flags and stdout state.
bool resolveColor(const diffmerge::cli::CliOptions& opts) {
    if (opts.colorExplicit) return opts.colorForced;
#ifdef Q_OS_WIN
    // On Windows we default to off; users can pass --color to force.
    return false;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

// Load a file, printing an error on failure. Returns true on success.
bool loadOrReport(const QString& path,
                  diffmerge::cli::LoadResult& out,
                  QTextStream& err) {
    out = diffmerge::cli::loadTextFile(path);
    if (!out.ok) {
        err << "diffmerge: " << out.errorMessage << '\n';
    }
    return out.ok;
}

// Run the diff engine on the loaded files and return the result.
diffcore::DiffResult runDiff(const QStringList& left,
                             const QStringList& right,
                             const diffcore::DiffOptions& opts) {
    diffcore::DiffEngine engine;
    return engine.compute(left, right, opts);
}

// Emit output in --brief mode. Returns the appropriate exit code.
int emitBrief(const diffcore::DiffResult& r,
              const QString& leftPath,
              const QString& rightPath,
              QTextStream& out) {
    if (r.isIdentical()) return kExitIdentical;
    out << "Files " << leftPath << " and " << rightPath << " differ\n";
    return kExitDifferent;
}

// Emit full output (headers + hunks + summary). Returns exit code.
int emitFull(const diffcore::DiffResult& r,
             const QStringList& leftLines,
             const QStringList& rightLines,
             const diffmerge::cli::CliOptions& opts,
             QTextStream& out) {
    if (r.isIdentical()) return kExitIdentical;

    diffmerge::cli::PrintOptions printOpts;
    printOpts.useColor = resolveColor(opts);

    out << "--- " << opts.leftPath << '\n';
    out << "+++ " << opts.rightPath << '\n';

    diffmerge::cli::HunkPrinter printer(out, leftLines, rightLines, printOpts);
    printer.printAll(r);

    // Summary footer.
    out << "\n"
        << r.stats.additions << " addition(s), "
        << r.stats.deletions << " deletion(s), "
        << r.stats.modifications << " modification(s)\n";
    return kExitDifferent;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const auto opts = diffmerge::cli::parseArgs(app.arguments());
    const QString programName = QStringLiteral("diffmerge");

    if (opts.showHelp) {
        out << diffmerge::cli::helpText(programName);
        return kExitIdentical;
    }
    if (opts.parseError) {
        err << "diffmerge: " << opts.errorMessage << '\n';
        return kExitError;
    }

    diffmerge::cli::LoadResult leftLoad, rightLoad;
    if (!loadOrReport(opts.leftPath, leftLoad, err))  return kExitError;
    if (!loadOrReport(opts.rightPath, rightLoad, err)) return kExitError;

    const auto result = runDiff(leftLoad.lines, rightLoad.lines,
                                 opts.diffOpts);

    return opts.brief
        ? emitBrief(result, opts.leftPath, opts.rightPath, out)
        : emitFull(result, leftLoad.lines, rightLoad.lines, opts, out);
}
