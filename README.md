# DiffMerge

DiffMerge compares text files through a Qt 6 desktop application and a command-line
interface. Both use `libdiffcore`, a line diff engine based on the O(NP)
Wu/Manber/Myers algorithm, with normalization options and slider placement
heuristics.

The GUI provides file and directory comparison, per-side editing, block copying
and safe saving. Embedded widgets are read-only by default. Three-way merging is
planned.

## License

DiffMerge's own code, including its libraries, applications and examples, is
licensed under the **GNU Lesser General Public License, version 3 only
(LGPL-3.0-only)**. See [LICENSE](LICENSE) for the LGPL-3 additional permissions
and [COPYING](COPYING) for the incorporated GPL-3 terms.

Under section 6 of LGPL-3.0, Andrzej Borucki, or a person he publicly designates,
is the proxy who may authorize a future LGPL version by a public statement.
Future versions are not automatically allowed; an accepted version becomes an
additional option, while LGPL-3.0 remains available. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the contribution and proxy policy.
Third-party code and dependencies retain their respective licenses.

## Requirements

- CMake 3.21 or newer.
- A C++20 compiler.
- Qt 6.2 or newer: Core for the library and CLI; Gui, Widgets, Svg and Concurrent for the widgets;
  Network for the corpus downloader; Test when building tests.
- qt-extra v2.3.0 for the desktop application, fetched from
  https://github.com/siplasplas/qt-extra.git. Widgets and Core do not depend on it.
- qcodeedit and qcodeedit-kate 1.6.0 or newer for the widgets and GUI; the desktop
  application also uses qcodeedit-katedata (Qt6 Network). CMake first looks for installed packages.
  If none is compatible, FetchContent downloads tag `v1.6.0` from
  https://github.com/siplasplas/qcodeedit.git. This requires Git and network access
  on the first configuration.

## Building

Run from the repository root after installing the required development packages:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

For a qcodeedit installation outside standard search paths, add
`-DCMAKE_PREFIX_PATH=/path/to/install` to the configuration command. The fetched
library is built without its demo or tests, with the Kate XML companion enabled.
The Kate downloader and Qt6 Network are needed only by the desktop application.
Missing companions are fetched even when the core editor is already installed.
Disabling both the GUI and widgets
removes the qcodeedit dependency.

All build options below default to `ON` for a standalone build. When included
with `add_subdirectory()`, only the core and widgets are enabled by default;
applications, tools and tests stay disabled.

| Option | Builds |
| --- | --- |
| `DIFFMERGE_BUILD_WIDGETS` | Embeddable Qt6 widgets library |
| `DIFFMERGE_BUILD_GUI` | Desktop application |
| `DIFFMERGE_BUILD_CLI` | Command-line file comparison |
| `DIFFMERGE_BUILD_CORPUS_DL` | Corpus download and annotation tools |
| `DIFFMERGE_BUILD_SLIDER_EVAL` | Slider evaluation tool |
| `DIFFMERGE_BUILD_TESTS` | Tests for enabled components and the core library |

For example, build only the core library and CLI, with tests:

```bash
cmake -S . -B build-cli \
  -DDIFFMERGE_BUILD_GUI=OFF -DDIFFMERGE_BUILD_WIDGETS=OFF \
  -DDIFFMERGE_BUILD_CORPUS_DL=OFF \
  -DDIFFMERGE_BUILD_SLIDER_EVAL=OFF
cmake --build build-cli --parallel
```

The default build produces:

- `build/libdiffcore/libdiffcore.a` — static core library on Unix-like systems.
- `build/diffmerge-cli/diffmerge` — CLI.
- `build/diffmerge-gui/diffmerge-gui` — GUI.
- `build/diffmerge-corpus-dl/corpus-dl` and `corpus-annotate` — corpus tools.
- `build/diffmerge-slider-eval/slider-eval` — evaluator.
- Test executables under each component's `tests/` directory, plus the GUI
  `screenshot_tool` helper.

Windows executables have an `.exe` suffix. Multi-configuration generators may
place executables in a configuration subdirectory such as `Debug/`.

## GUI

```bash
# Compare two files immediately.
./build/diffmerge-gui/diffmerge-gui fileA.txt fileB.txt

# Compare two directories immediately.
./build/diffmerge-gui/diffmerge-gui directoryA directoryB

# Choose paths interactively.
./build/diffmerge-gui/diffmerge-gui
```

Use **File > Open two files...** or **File > Open two directories...** to switch
views. Passing a single path pre-fills the left path field. Two paths must both
exist and both be files or directories. Mixed types and missing paths produce an
error on stderr and in a dialog, then exit with status 2; extra paths are rejected.
`--help` describes the arguments. `-L LABEL` / `--label LABEL` may be repeated
for two display labels; a label's file name supplies syntax rules when the
input file name has no matching definition (useful for temporary difftool files).

```bash
git config difftool.diffmerge.cmd 'diffmerge-gui "$LOCAL" "$REMOTE"'
./build/diffmerge-gui/diffmerge-gui -L src/old.cpp -L src/new.cpp old.tmp new.tmp
```

**View > Side by Side / Unified** switches file presentation. The desktop app
offers a live slider under **View > Panel spacing** (8–160 logical pixels,
default 48) and remembers the width of the connector between the panes. This
allows trying compact and spacious layouts at different window heights; the
width is currently manual, with no automatic height adjustment.
The desktop app
remembers this choice and **View > Skip unchanged lines**. Unified shows old
and new line numbers and `−`/`+` markers in its gutter; the document contains
only code, so copying code does not include these markers. Removed lines precede
added lines within each changed region. Since both versions share one pane, old
lines have a red background and new lines a green one, with stronger red and
green tints for the words and characters that differ; side by side keeps green
for one-sided changes and blue for replacements. Both modes retain word and
character highlighting and syntax colors computed over each complete original
file. Line numbers of changed rows are darker than those of unchanged rows.

Skipping replaces equal runs outside the context with `⋯ N unchanged lines —
click to show`. The default context is three lines around changes. Clicking a
placeholder expands the run in both side-by-side panes. Navigation, reveal and
search continue to use original file coordinates; reveal and search expand any
needed hidden lines. Switching modes preserves the top visible original line.
Function folding remains disabled.

File comparison provides:

- Two qcodeedit panes with line numbers on the inner edges and vertical
  scrollbars on the outer edges.
- Synchronized vertical scrolling across corresponding changes. Both side-by-side
  panes also share the horizontal offset and scroll range: moving either scrollbar
  or scrolling to the caret shifts both panes by the same number of columns, even
  when the other pane contains only short lines. Horizontal scrolling preserves
  both panes' vertical positions and the connector geometry, including partially
  visible blocks opposite an insertion/deletion boundary.
- Previous/next change navigation with **Shift+F7** and **F7**, or toolbar buttons.
- Three-level comparison: line alignment, word matching within replacement
  blocks, then character comparison within corresponding changed words.
  **View > Highlight whole changed words** optionally omits character refinement:
  `alpha_name` versus `alpha_game` highlights both whole words, including their
  common letters. The desktop remembers the choice. Switching recomputes
  highlights without discarding edits or Undo history.
  Insertions, deletions and replacements have line backgrounds; changed text
  fragments receive stronger highlighting, with added/removed words marked whole.
- One-sided changes use green backgrounds in either pane and a green boundary
  line on the side without a block. Replacements use blue backgrounds.
- The draggable center divider connects corresponding blocks with colored
  curves, narrowing to a line when a block exists on only one side.
- Block colors extend through the inner line-number margins. Connectors follow
  scrolling, resizing and change navigation, with offscreen parts clipped.
- A color scheme that follows the system's light or dark palette, including
  palette changes while the application is open.
- Line matching first preserves exact matches, then refines unmatched blocks
  ignoring indentation and trailing spaces/tabs, and finally interior spaces/tabs.
  Original text is still compared: interior spacing and literal contents remain
  visible changes. This does not equate lines split or joined by a formatter.
- Editable path fields and file selection buttons for reloading comparisons.

Directory comparison shows a six-column table for the current directory:
name, left size/time, status, and right size/time. Directories come first; names
remain case-sensitive. Enter or double-click a directory to enter it on both
sides, including directories present on only one side. `..` and Backspace go
up, selecting the directory just left. Typing a name's initial uses the table's
keyboard search. Open a file to compare it; a one-sided text file uses an empty
other side. **Directories** or Backspace in the file view returns to the retained
directory and selection.

Scans run on a worker, show per-file byte progress, and offer **Cancel scan**;
changing roots/options also cancels the previous scan. Equal-size regular
files are compared in cancellable 64 KiB chunks, regardless of timestamps.
The 64 MiB limit applies to optional text normalization; larger files are
compared byte-for-byte without normalization. Different sizes imply a difference; read errors
are displayed. Directory statuses summarize their descendants, including
one-sided subtrees. Symbolic-link directories are not traversed. The default
entry and nesting limits are 100,000 entries and 128 levels.

**View > Show differences only (directories)** hides equal entries and equal
subtrees. Equal entries have a white background; one-sided entries are red or
green, and modified entries are yellow. **Hide empty directories** independently
hides directories whose scanned subtree contains no files, including one-sided
empty directories. **Ignore line endings (directories)** treats CRLF, LF and CR
as equivalent within the comparison size limit. Interior spaces and a missing
final newline still count as differences; data containing NUL stays byte-exact.
The equality filter also uses this optional equivalence. These options do not
change files on disk. **Directory exclusions...** edits wildcard name patterns, defaulting
to `.git`, `build`, `build-*`, `cmake-build-*`, `node_modules`, `__pycache__`.
Excluded names are neither scanned nor included in directory status. The
desktop app remembers these choices; widgets do not persist preferences.

The View menu also provides **Ignore whitespace differences**, **Ignore trailing
whitespace** and **Ignore case**, shared by file and directory comparisons.
Whitespace equivalence trims the edges and collapses internal runs to one space;
it does not remove every internal space. The desktop accepts the matching CLI
flags `-w`, `-b`, `-i` (and their long forms). File comparisons are recomputed
asynchronously when these options change, preserving edits, their modified state,
and Undo/Redo. Ignored differences produce no change blocks. Directory comparison
uses the same normalization on valid UTF-8 files within the size limit; binary
and invalid UTF-8 inputs remain byte-exact. Directory line-ending equivalence
remains a separate option. The file view already compares lines without their
terminators. Embedded hosts choose options with `FileDiffWidget::setDiffOptions()`
and `DirDiffWidget::setDiffOptions()`; labels and save targets are preserved.

The desktop watches compared file targets and their parent directories. While
the file view is open, external changes automatically reload an unmodified side,
keeping its viewport and the other side's edits and Undo history. For a modified
side, it asks whether to reload and discard that side's edits or keep them.
Keeping edits does not bypass the external-change check when saving. Watches are
renewed after atomic replacement; saving through the desktop does not trigger a
redundant reload. Failed reloads keep the existing documents intact. A file that
becomes binary or large switches to byte comparison when both sides are clean;
otherwise existing edits remain intact and the host must save or discard them first.

**View > Refresh**, F5 in the file view, and Ctrl+R reread compared files with the
same per-side discard confirmation. Ctrl+R rescans directories; F5 there still
copies left to right. Embedded hosts can call `reloadSide(side)` and explicitly
authorize discarding that side's edits with `reloadSide(side, true)`. File watching,
confirmation dialogs, and shortcuts belong to the desktop application.

**View > Swap sides** reverses the current file or directory comparison. Embedded
hosts can call `FileDiffWidget::swapSides()` or `DirDiffWidget::swapSides()`.
File editors retain their document identity, unsaved edits, Undo history, syntax
rules and save targets. Labels and edit permissions follow their files; comparison
ranges are recalculated and transient search highlights are cleared. A directory
swap retains the relative directory and selection and rescans the reversed roots.
Binary comparisons restart with reversed paths and still support cancellation.

Select rows and use **Copy →**, **← Copy**, Alt+Right/Alt+Left, or F5 (left to
right). These copy files/directories, not text blocks. The desktop asks first,
listing destination entries and marking files to overwrite. Regular files
are copied atomically with their permissions, directories recursively; a failed
multi-file copy may leave earlier successful copies and is followed by a rescan.
Changed sources/destinations detected since confirmation refuse the copy.
Disjoint roots are required; symbolic links, escaping paths and file/directory
collisions are refused. **Delete left/right** asks before moving selected entries
to trash; failure never falls back to permanent deletion. All operations also
appear in the table context menu. Ctrl+R or **Refresh** rescans.

Embedded `DirDiffWidget`s default to both sides read-only. Hosts can opt in with
`setReadOnly(Side, false)` and handle `copyRequested` / `deleteRequested` signals;
only the desktop host performs confirmed writes. `setExclusions()`,
`setDifferencesOnly()`, `currentRelativeDirectory()`, `navigateInto()`,
`navigateUp()`, `refresh()`, `scanFinished` and `operationFailed` expose navigation
and scanning independently of the desktop menus.

### Syntax highlighting

File comparisons select Kate XML syntax definitions independently for each side
using the file name. Syntax foreground colors and font styles remain visible over
the existing diff block and character backgrounds. Comments and strings retain
their syntax state across original document lines. Colors adapt to the light or
dark diff scheme. Syntax folding and its markers stay disabled; equal-line
placeholders are managed separately by the comparison view.

The desktop application checks qcodeedit's shared Kate data at startup using the
local manifests and XML/theme files. If data is missing or incomplete, it offers
to download the supported Kate definitions and themes from kate-editor.org and
invent.kde.org. Downloads are asynchronous, with status-bar progress; open
comparisons refresh afterward. A declined offer is remembered. **Tools > Update
Syntax Definitions...** retries the download even after a previous refusal.
Without definitions or when offline, comparison and diff highlighting still work.

The data directory is supplied by qcodeedit (`qce::kate::dataDir()`), normally
`~/.local/share/qcodeedit/kate-<major>.<minor>/` on Linux, with XML files in `syntax/`.
`QCE_KATE_DATA_DIR` selects an alternate directory. The widgets only read local
syntax data: they do not download, write the index, ask questions or add Qt Network
to an embedding application. Hosts manage updates and call
`FileDiffWidget::reloadSyntaxDefinitions()` when local definitions change.

For in-memory comparisons, `TextSnapshot::fileName` is an optional syntax hint,
separate from the arbitrary display label:

```cpp
auto snapshot = diffmerge::gui::TextSnapshot::fromText(
    sourceText, "Selected revision", "src/example.cpp");
```

`loadFromPaths()` supplies the real file names automatically. With `setContent()`,
call `setSyntaxFileName(Side::Left, "example.cpp")` and the equivalent for the
right side afterward. Replacing or clearing the comparison resets its syntax hints
from the new snapshots. An unknown file name or absent XML uses plain text plus
diff colors. XML loading and editor syntax highlighting happen on the GUI thread;
worker comparison preparation stays independent of syntax data and GUI resources.

### Editing and saving

The desktop application enables editing on the right, with the left read-only.
`--edit right|left|both|none` chooses the sides; `--readonly` is `--edit none`.
This choice also controls destination writes and deletions in the directory view.
**View > Edit left/right side** changes that choice. A lock or pencil beside the
path shows the effective state, with `*` for unsaved changes; the window title
also marks modified content. Editing requires **Side by Side** without skipped
unchanged lines. Projected views stay read-only and display a short hint;
switching presentation or locking a modified side requires saving/discarding first.

Changes recalculate the comparison on a cancellable worker after a 300 ms
debounce. Stale results are discarded. Installing the result updates colors,
connectors and scroll mapping without replacing editor documents, cursors,
selections or undo history. While the result is pending, obsolete blocks and
copy arrows are unavailable. Ctrl+Z and Ctrl+Shift+Z/Ctrl+Y use each editor's undo.

Arrows in the connector copy a whole change block towards an editable side;
Alt+Right/Alt+Left copy the current change and navigate on. Insertions, deletions
and replacements each form one undo step. Copying an empty range removes the
opposite block. Clicking a connector arrow preserves both viewports, including
independently positioned block endpoints, during copying and recomputation.
Arrows are painted above all connectors, including overlapping neighbouring
curves. No arrow targets a read-only side. Copy arrows use the normal mouse pointer;
the rest of the divider uses the panel-resize cursor.

**File > Save** (Ctrl+S) saves the focused side; **Save Left/Right/Both** are also
available.
Each modified path also has its own **Save** button: saving one side leaves the
other side modified and its Save button available. Embedded hosts handle these
buttons through `saveRequested(Side)`.
Opening another comparison, returning to directories or closing asks
**Save / Discard / Cancel** for unsaved changes. Saving a directory file rescans
its statuses. A missing side receives the corresponding destination path; Save
can create it (and its parents).

UTF-8 BOMs, unchanged lines' mixed CR/LF/CRLF endings, final newline and file
permissions (including executable bits) are preserved. New lines use the first
known delimiter, defaulting to LF. Writes use `QSaveFile` at the canonical target,
preserving symbolic links. Changed on-disk bytes require explicit overwrite
confirmation. A changed symlink target refuses saving until explicitly reselected.
Non-UTF-8 text is shown with a read-only note; it cannot be saved or used as a
text-block copy source with replacement UTF-8 characters. Binary inputs show a
byte-equality/size summary with both sides read-only.
Files exceeding the 8 MiB text preview limit or text preparation limits use byte comparison instead of
failing. A NUL byte in the first 8000 bytes triggers binary mode before decoding,
as in Git's content heuristic; NUL found later also prevents text editing.
Byte comparisons run on a worker with 64 KiB buffers, a progress bar and processed
byte count, and **Cancel comparison**. Equal sizes are verified by reading bytes;
different sizes or the first differing chunk finish early. Cancellation displays
an unknown result, never "identical". Read errors and detected changes during
comparison are reported without claiming equality. Binary/large inputs remain
read-only. Text loading remains on the GUI thread.

Embedded hosts can inspect `byteComparisonStatus()`, receive
`byteComparisonProgress(perMille, detail)` and `byteComparisonFinished(status)`,
and call `cancelByteComparison()`. `loadFromPaths()` returns when byte comparison
starts; its asynchronous result is separate from `changeCount()`.

Embedded widgets remain read-only by default. Hosts use `setEditable(Side,bool)`,
`setSaveTarget(Side,path)`, `text(Side)`, `isModified(Side)`, `save(Side,error)` and
`modifiedChanged(Side,bool)`; snapshot labels/file names are never inferred as
save paths. `text()` returns LF-normalized text including its final newline, so
hosts performing their own writes must handle encoding and newline serialization.
`setComparison()` and path loading refuse modified content: the host asks the
user, saves or calls `discardChanges()`, then installs another comparison. Widgets
show no dialogs. `copyChange(index,source)` provides block copying independently
of desktop shortcuts. `operationFailed` reports refused operations or worker errors.

### Current limitations

- Editing and text block copying require the full side-by-side presentation.
- There is no overview minimap or three-way merge.
- The editor displays real document lines without visual filler rows, so panes
  can have different heights around insertions and deletions.
- Directory copies refuse symbolic links and file/directory type collisions;
  an operation spanning multiple entries is not one atomic transaction.
- Directory ignore options apply only within the text-normalization size limit.
  Trash support depends on the platform; unsupported trash never deletes permanently.

## CLI

```bash
./build/diffmerge-cli/diffmerge fileA.txt fileB.txt
./build/diffmerge-cli/diffmerge -iw --brief fileA.txt fileB.txt
./build/diffmerge-cli/diffmerge --help
```

Output uses normal diff-style `a`, `d` and `c` hunk headers, with `<` and `>` line
prefixes. Unified/context output and recursive directory comparison are not
implemented in the CLI.

| Option | Effect |
| --- | --- |
| `-i` | Ignore case |
| `-w` | Collapse whitespace runs and trim leading/trailing whitespace |
| `-b` | Ignore trailing whitespace only |
| `--brief` | Report only whether the files differ |
| `--color` / `--no-color` | Force or disable ANSI colors |
| `-h`, `--help` | Show help |

Exit codes are `0` for identical files, `1` for differences and `2` for errors.

## Bounded change counting

`DiffEngine::countChanges(left, right, options, limits, cancellation)` counts
added and removed lines using the O(NP) search without recording a path, hunks
or slider adjustments. Memory is linear in the input. It trims equal prefixes
and suffixes and can stop with `TooManyDifferences`, `TimedOut` or `Cancelled`;
counts are valid only when the status is `Complete`.

```cpp
diffcore::CountLimits limits{.maxEditDistance = 2000, .timeLimitMs = 100};
const auto counts = diffcore::DiffEngine{}.countChanges(before, after, {}, limits);
if (counts.status == diffcore::ChangeCounts::Status::Complete) {
    // counts.added and counts.removed are available.
}
```

Nonpositive limits are unlimited. The time limit covers normalization and search.
Cancellation accepts a copied `CancellationToken` or a borrowed atomic flag in
`CountLimits::cancel`, which must remain alive for the call. Normalization follows
`DiffOptions`; `alignWhitespaceChanges` only affects presentation in full diffs,
so bounded counting uses exact normalized-line matches.

## Core sequence API

`diffcore::SequenceDiff::compute(left, right)` compares sequences of elements
through the same O(NP) engine used for line comparison. Include
`<diffcore/SequenceDiff.h>` and link `diffcore::diffcore`. Inputs can be strings,
vectors of token IDs or equality-comparable tokens, or `std::span` views. Both
inputs use the same container type with `value_type`, `size()` and indexing.

```cpp
const auto result = diffcore::SequenceDiff::compute(
    std::string("oldName"), std::string("newName"));
```

The result contains hunks, `leftSize`, `rightSize` and an insert/delete
`editDistance`. Hunk ranges index original input elements with half-open bounds;
empty ranges mark insertion boundaries. `SequenceDiffOptions` controls replacement
merging and coalescing, both enabled by default. Comparison is exact, without
normalization or line slider heuristics. Inputs too large for the engine's integer
coordinates throw `std::length_error`. For `QString`, offsets count UTF-16 units;
for `std::string`, they count bytes.
The GUI uses this adapter first for words within each replacement block, then
for characters within corresponding changed words. Added or removed words are
highlighted in full; punctuation and spacing remain significant. Word matching
spans the block's lines and results map back to original UTF-16 editor columns.
In the default character mode, splitting or joining words by changing only
spacing highlights that spacing. Whole-word mode highlights the complete
unmatched tokens and skips character comparison. Embedded hosts set
`ComparisonOptions::highlightDetail = IntraLineDiffEngine::Detail::WholeWords`
before `prepareComparison()`, or call
`FileDiffWidget::setHighlightDetail(IntraLineDiffEngine::Detail::WholeWords)`.
`Detail::Characters` restores the default three-level highlighting. This changes
only the stronger text highlights, not line blocks, equality or block-copy actions.

For line comparisons, `DiffEngine` accepts `DiffOptions::alignWhitespaceChanges`
to enable the GUI's spacing-aware alignment without hiding differences. The
library defaults to strict alignment; the GUI enables refinement by default.
Refinement preserves matching anchors instead of applying slider heuristics.
Its edit cost describes the resulting alignment and is not necessarily minimal.

## Embedding in a Qt6 application

The desktop executable and reusable widgets share the same implementation.
Two static libraries are available; both propagate the C++20 requirement:

| CMake target | Purpose | Dependencies |
| --- | --- | --- |
| `DiffMerge::Core` | Line and generic sequence diff algorithms | Qt6 Core |
| `DiffMerge::Widgets` | File and directory comparison widgets | Core, Qt6 Widgets/Svg/Concurrent, qcodeedit + qcodeedit-kate >= 1.6.0 |

For a source dependency (including CMake FetchContent), use:

```cmake
add_subdirectory(external/diffmerge)
target_link_libraries(history-viewer PRIVATE DiffMerge::Widgets)
```

This builds the libraries without the standalone applications, tools or tests.
To use only the core, set `DIFFMERGE_BUILD_WIDGETS=OFF` before adding the directory.
The legacy source target `diffcore::diffcore` is also retained.

For an installed package:

```bash
cmake -S . -B build-lib -DDIFFMERGE_BUILD_GUI=OFF \
  -DDIFFMERGE_BUILD_CLI=OFF -DDIFFMERGE_BUILD_CORPUS_DL=OFF \
  -DDIFFMERGE_BUILD_SLIDER_EVAL=OFF -DDIFFMERGE_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build-lib --parallel
cmake --install build-lib
```

The consuming project then uses:

```cmake
find_package(DiffMerge 1.3 CONFIG REQUIRED COMPONENTS Widgets)
target_link_libraries(history-viewer PRIVATE DiffMerge::Widgets)
```

Pass `-DCMAKE_PREFIX_PATH=/path/to/install` when configuring the consumer.
`COMPONENTS Core` loads only the algorithm library and requires neither Widgets
nor qcodeedit. With no components specified, all available libraries are loaded.
If qcodeedit was fetched, its package is installed alongside DiffMerge; an
existing qcodeedit installation remains an external dependency.

Public widget headers live under `include/diffmerge/`. For example, a history
viewer can feed revision contents directly, without temporary files:

```cpp
#include <diffmerge/FileDiffWidget.h>

auto* diff = new diffmerge::gui::FileDiffWidget(parent);
diff->setPathBarVisible(false);
diff->setNavigationBarVisible(false); // The host can provide its own controls.
diff->setViewMode(diffmerge::gui::ViewMode::Unified);
diff->setUnchangedLinesSkipped(true);
diff->setContextLines(3);
diff->setContent(parentRevisionLines, selectedRevisionLines);
```

`setContent()` takes original lines without newline delimiters, copies the
content, and computes the comparison synchronously on the GUI thread. The
library installs no keyboard shortcuts; hosts call `navigateToNext()` and
`navigateToPrev()` and choose their own keys. The desktop application and example
bind F7 / Shift+F7 to the focused comparison. Folding is not enabled in the diff
editors. To customize the editor
colors, include `<diffmerge/DiffEditor.h>` and `<diffmerge/ColorScheme.h>` and
use `leftEditor()` / `rightEditor()`. `DirDiffWidget::fileActivated` provides
paths for connecting directory browsing to a file comparison. `loadFromPaths()`
emits `loadFailed(message)` on read failure; hosts loading revision blobs should
use `setContent()` or the prepared-comparison API below. Browse buttons emit `fileBrowseRequested` or
`directoryBrowseRequested`; the host opens its preferred picker and calls
`setPath(side, path)`. The desktop application uses qt-extra dialogs, while the
libraries require no qt-extra and open no file or error dialogs.

For worker preparation, include `<diffmerge/Comparison.h>`:

```cpp
using namespace diffmerge::gui;
auto left = TextSnapshot::fromText(parentText, "Parent revision");
auto right = TextSnapshot::fromText(selectedText, "Selected revision");
diffcore::CancellationToken cancellation;
// Run on a worker, capturing snapshots and the token by value:
auto result = prepareComparison(left, right, {}, cancellation);
// Deliver to the GUI thread after checking the host's current job generation:
if (result.status == PreparationStatus::Ready)
    diff->setComparison(result.comparison);
// A newer host request can call cancellation.requestCancellation().
```

`PreparedComparison` owns immutable snapshots, original ranges, the aligned
model, intra-line highlights and scroll mapping. Its `shared_ptr<const ...>` can
outlive worker locals and be shared across views. `setComparison()` runs on the
GUI thread without recomputing a diff; replacing it or calling `clearComparison()`
detaches the previous data and clears transient overlays. The host owns worker
scheduling and rejects stale results using its own generation ID. The example
shows this with QtConcurrent. File comparison preparation itself uses QtCore;
directory widgets use QtConcurrent internally for cancellable background scans.

`PrepareResult::status` distinguishes `Ready`, `Cancelled`, `ResourceLimit` and
`Error`. Only `Ready` contains a comparison. Cancellation is cooperative inside
algorithm loops; allocation, Qt string operations and cleanup are not hard
real-time operations. `ComparisonOptions::limits` defaults to 100,000 combined
input lines, 4,000,000 combined UTF-16 units (charging one extra unit per line),
200,000 units per line, 20,000,000 work units and 1,000,000 recorded O(NP) steps.
These are input/work budgets rather than an exact byte allocation cap. Hosts can
adjust them; integer-coordinate limits still apply. The synchronous `setContent()`
wrapper uses the same default limits and emits `loadFailed` on preparation failure,
retaining the previous comparison. Core clients can pass a worker-local
`diffcore::ComputationControl` to the sequence or line engine; stopping throws
`ComputationStopped` with a cancellation or resource-limit reason.

Host navigation uses original, zero-based document coordinates:

```cpp
diff->revealLines(Side::Left, {deletedLine, 1}, true);
diff->revealText(Side::Right, {addedLine, utf16Column, utf16Length}, true);
diff->setSearchHighlights(Side::Right, {{addedLine, utf16Column, utf16Length}});
diff->navigateToChange(changeIndex);
diff->clearSearchHighlights();
```

`changeCount()`, `currentChangeIndex()` (`-1` for no selected change), `changes()`,
`currentChangeChanged(index)` and `comparisonChanged(count)` support host-owned
controls. Change indices belong to DiffMerge; another diff engine's hunks must be
mapped through original coordinates. Revealing a range synchronizes the opposite
pane and clears the selected change index. Line ranges are half-open; a zero
count represents a boundary, including `lineCount` at EOF or `0` in an empty file.
Text ranges use UTF-16 columns on one line; a zero length represents a boundary,
and at EOF only `{lineCount, 0, 0}` is valid. Invalid ranges return `false` without
changing the view. Search overlays are independent of diff data; clearing them
restores comparison colors. Positive text ranges use character overlays; zero
length search hits use a horizontal line boundary marker. Optional reveal emphasis
covers the requested lines, or the EOF boundary, until cleared or replaced.

Snapshots omit line terminators but retain per-line LF/CRLF/CR metadata and optional
final-newline state. `fromText("")` is an empty file; `fromText("\n")` contains one
empty terminated line. Legacy `QStringList` input has unknown metadata. Labels,
line endings and final-newline state remain available through the snapshot API;
the comparison widget does not display a metadata row. Original documents
retain their code; projected displays add only unchanged-line placeholders.
End-of-line differences do not create textual change
blocks. Binary detection, decoding, repository access and revision identity belong
to the host.

`PrepareResult::preparationTime` and `lastInstallationTime()` report computation
and GUI installation separately. GUI document installation and stored aligned
rows/highlights still consume time and memory; this implementation does not
virtualize large documents.

A baseline measurement before view projections (Linux, Qt 6.10, Release build,
offscreen, one run) requested
cancellation after 1 ms of worker time, with a higher trace budget so cancellation
could be observed rather than hitting that budget first:

| Input on each side | Cancellation request to worker return |
|---|---:|
| 4,000 repeated lines, all `a` versus all `b` | 0.12 ms |
| One 50,000-unit word, all `a` versus all `b` | 0.07 ms |

For 10,000 equal short lines per side, preparation took 1.82 ms and GUI installation
0.14 ms, excluding the subsequent first paint. Peak resident memory for the whole
benchmark process, including Qt and earlier workloads, was about 39 MiB; this is
not an isolated model allocation measurement. These observations describe that
run, not hard latency or memory guarantees. Version 1.2 also builds display maps
and caches original-side syntax when a projected view is needed; the installation
timing above is a historical baseline, not a measurement of these new modes.

A small history-viewer example is included (it displays sample revisions,
without a Git dependency):

```bash
cmake -S examples/embedded-diff -B build-example \
  -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build-example --parallel
./build-example/embedded-diff
```

Alternatively, pass `-DDIFFMERGE_SOURCE_DIR=/path/to/diffmerge` to build the example
against a checkout. `QT_QPA_PLATFORM=offscreen ./build-example/embedded-diff --smoke`
checks embedding and resource loading without opening a window.

### Merge session inputs

`<diffmerge/MergeSession.h>` adds `prepareMergeSession()` to `DiffMerge::Widgets`.
It can run on a worker without GUI resources and produces an immutable
`PreparedMergeSession`. This first stage prepares source data for inspection;
the standalone merge algorithm remains a subsequent stage.

Pass independent `MergeFileInput` descriptors for BASE, OURS and THEIRS. Each owns
its bytes, opaque source/object identity, raw path, optional mode, display label
and syntax hint. `Present` with zero bytes is an existing empty file; `Absent`
and `Unknown` are separate states and have no text snapshot. Role names have no
branch, filesystem or operation interpretation in the library.

An optional `MergeResultSeed` retains an existing working buffer, host-generated
merge buffer or retained result, including its opaque fingerprint. The seed is
never replaced by an automatically generated merge. Original UTF-8 bytes, BOM,
per-line LF/CRLF/CR endings and final-newline state remain available independently
of display text. The host owns encoding conversions and stale-input checks.

Optional `hostConflicts` distinguish an unavailable conflict list from an
explicitly empty list. Conflict IDs must be unique and nonempty; optional source
and result line ranges use original, half-open coordinates and must be within an
available text snapshot. Resolution states are supplied explicitly, never inferred
from the presence or absence of markers. Missing result ranges remain unmapped.
Session preparation does not parse markers or perform resolution, export, save
or repository operations.

Preparation reports `Ready`, `Cancelled`, `ResourceLimit`, `Error` or `Unsupported`.
Only `Ready` contains a session. Aggregate byte, metadata, line, UTF-16, per-line,
conflict-count and work limits cover all four inputs. Binary content (NUL), invalid
UTF-8 and present symlink/submodule/other file kinds are unsupported for text
preparation and never become placeholder text. Owned input buffers must already
exist before preparation; limits do not bound allocations made by the host.

### Conflict marker import and read-only merge preview

`importConflictMarkers()` in `<diffmerge/ConflictMarkers.h>` reads the existing
RESULT of a prepared session. Its state machine imports merge and diff3 marker
blocks, including zdiff3 blocks with common text outside the delimiters. Pass the
configured positive `markerSize` (default 7); LF, CRLF, CR, empty fragments,
optional labels and an unterminated final line are supported. Each imported region
has original RESULT line and byte ranges, exact fragment bytes, and a stable ID
within the unchanged seed. Byte ranges exclude the file BOM and include marker
line terminators. BASE fragments are optional and never imply a complete ancestor.

Nested, missing, repeated, orphan or out-of-order delimiters are diagnostics.
Failures publish no partial conflict list. Import honors resource limits and
cancellation and never rewrites RESULT or changes host resolution states. Removing
markers does not imply a resolved session. Marker recognition requires host
conflict metadata or explicit `allowUnconfirmedMarkers` consent. A host can mark
known literal marker lines with `literalMarkerLines`; these remain ordinary text.

`MergePreviewWidget` in `<diffmerge/MergePreviewWidget.h>` is a read-only view of
OURS / RESULT / THEIRS, with optional BASE. Pass a prepared session to
`setSession(session, markerOptions)`. Available complete sources stay complete;
when a source is unknown or absent, selected marker fragments are explicitly
labelled as fragments rather than reconstructed source files. The entire RESULT,
including surrounding manual edits and markers, stays visible. Invalid replacement
imports keep the previous session. The header reports marker regions separately
from the host's unresolved count or unknown resolution state.

Use `navigateToConflict()`, `navigateToNextConflict()` and
`navigateToPreviousConflict()` for navigation, `setBaseVisible()` to inspect BASE,
and `setPanelSpacing()` for the gaps (default **24 px**, range 8–160). Two-pane
comparisons retain their **48 px** default. The preview does not yet align source
fragments with RESULT, draw merge connectors, edit, save or resolve conflicts.

The desktop offers **File > Open conflict editor...** in a separate window, with F7 / Shift+F7 navigation, configurable marker length and a spacing
slider. The same keys navigate differences in two-pane comparisons and marker
conflicts in the merge preview; key bindings belong to the desktop, not the widget.
Input loading and session preparation run on a worker; closing the window cancels preparation.
The desktop reads only the selected regular file and does not read Git stages or
change the repository. This preview is not yet a `git mergetool` resolver.

### Editable RESULT and conflict decisions

`MergeWidget` in `<diffmerge/MergeWidget.h>` extends the preview with an editable
RESULT. It starts read-only; call `setEditable(true)` to enable editing. OURS,
BASE and THEIRS remain immutable reference material. Neither switching edit mode
nor making decisions writes a file or changes a repository.

`chooseConflict(index, choice)` takes OURS, THEIRS, BASE, both sides in either
explicit order, or deletes only that fragment. Combined choices concatenate the
exact fragments without adding a separator. Missing sources and unavailable BASE
are disabled; an existing empty fragment is a valid choice. Host ranges that
exactly match imported marker ranges share one conflict identity, and available
host source fragments take precedence over marker fragments. Unmapped host
conflicts remain separate and unresolved rather than being guessed or dropped.

`conflicts()` exposes stable IDs, current half-open UTF-16 ranges in normalized
RESULT, mapping validity, explicit resolution states and chosen provenance.
Edits before a conflict move its range; edits inside it require review. Removing
or crossing its boundary makes the mapping ambiguous and disables fragment
replacement. `reviewConflictRange()` (or **Review selected range**) supplies an
explicit new range. `markConflictResolved()` confirms a reviewed manual result;
`markConflictUnresolved()` restores its unresolved state without discarding text.
Deleting markers alone never resolves a conflict.

Fragment choices, range review and state decisions each use one step on the
result editor's native Undo stack. Native typing and Undo/Redo restore conflict
states and original line endings with the text. History is bounded to 32 steps;
the editor accepts at most 4096 conflicts. `MergePreviewWidget` remains available
for larger read-only inspections. Overlapping mapped ranges refuse fragment
replacement until reviewed. Navigation uses current RESULT coordinates;
`navigateToNextUnresolvedConflict()` and `navigateToPreviousUnresolvedConflict()`
skip explicitly resolved conflicts. `markerConflicts()` retains the original
imported marker regions, separate from the current editable ranges.

`resultText()` returns normalized editor text; `resultBytes()` returns exact
current UTF-8 bytes with the RESULT seed's BOM and retained per-line endings.
Unchanged CRLF/CR/LF terminators survive; new manually inserted line breaks use
the preceding available terminator, falling back to the seed's first or LF.
Chosen fragments keep their own terminators. Serialization returns `nullopt`
for unsupported text or exceeded limits, never silently replacing invalid data.
The session's original bytes, source identities and fingerprint remain immutable.

Use `isModified()`, `unresolvedCount()`, `modifiedChanged`,
`unresolvedCountChanged` and `conflictStatesChanged` for host controls. Replacing a
modified session is refused; `discardChanges()` explicitly restores its initial
RESULT and conflict states. Zero unresolved conflicts is an editor state, not
an accepted, saved or staged resolution. Structured export is described below;
save requests and desktop filesystem saving are described below.

The desktop has **Edit RESULT**, fragment buttons, range review and native
Undo/Redo. F7 / Shift+F7 navigate conflicts; the three-pane gap stays 24 px.
Saving and close prompts are described below. The desktop is not yet a complete
mergetool with command-line input roles and resolution exit status.

### Structured merge export

`<diffmerge/MergeExport.h>` defines the owned `MergeExportInput` and
`prepareMergeExport()`. Capture `MergeWidget::captureExportInput()` on the GUI
thread, then pass that independent snapshot to a worker. `exportResult()` is a
synchronous convenience wrapper. Neither function writes a file, clears the
modified flag, changes editor history, stages content or closes the widget.

`MergeExportOptions::disposition` selects **Cancelled**, **Draft** or **Resolved**.
A deliberate Cancelled request returns a Ready outcome with no payload; worker
cancellation returns Cancelled status with no outcome. Errors and resource limits
also return no partial outcome. All output values are owned. The outcome retains
its immutable captured session (source identities, raw paths, modes and original
bytes), the RESULT fingerprint, current conflict IDs/states/provenance and output
UTF-8 serialization metadata (BOM, final newline and each line ending).

**Draft** retains unresolved regions as complete, well-formed markers. The default
`MergeMarkerStyle::Preserve` preserves exact current bytes, including mixed line
endings and manually edited marker fragments. Other styles explicitly select
merge/diff3/zdiff3, marker length, delimiter line endings and optional single-line
labels. Conversion keeps the current fragments and common text outside conflicts;
zdiff3 output preserves existing common-text placement rather than computing new
factoring. Diff3/zdiff3 requires an existing BASE fragment. Exported conflict ranges
refer to the resulting normalized text, including changed delimiter lengths.
Unmapped/overlapping ranges, untracked/malformed markers, or unresolved fragments
whose markers were manually removed are refused with a diagnostic. Export never
reconstructs over a manual edit to manufacture a draft. Explicitly literal marker
lines can be retained in an unchanged seed; edited text with such consent requires
coordinate review before export: update the captured input's literal line set
and explicitly set `literalMarkerLinesReviewed`.

**Resolved** requires writable mode, all conflicts explicitly resolved, valid
mapped ranges and no remaining nonliteral markers. When the authoritative host
conflict list is unknown, the caller must additionally set
`confirmUnknownConflictState`; removing markers alone is insufficient. The outcome's
`explicitlyCompleted` records the editor decision only, without claiming host
acceptance, saving or staging.

`MergeFileAction::Keep` returns bytes; zero bytes means an existing empty file.
`MergeFileAction::Delete` requires Resolved and `confirmFileDeletion`. This is a
whole-file decision, distinct from deleting a conflict fragment: it explicitly
resolves the captured conflicts as deletion, with no text serialization payload.
It never deletes anything on disk. `rawPath` and `mode` default to RESULT seed
metadata; the host can explicitly provide a different opaque path or mode. No
output path is required for in-memory buffers. The host validates stale fingerprints,
path/mode conflicts, filters and publication before accepting the outcome.

```cpp
auto captured = widget.captureExportInput(); // GUI thread
if (captured) {
    diffmerge::gui::MergeExportOptions options;
    options.disposition = diffmerge::gui::MergeExportDisposition::Resolved;
    // Set confirmUnknownConflictState only after explicit host/user review.
    auto exported = diffmerge::gui::prepareMergeExport(*captured, options);
    // Only a Ready outcome may be considered for host acceptance.
}
```

### Save requests and desktop saving

`MergeWidget::requestSave()`, `requestExport()` and `requestFinish()` emit
`saveRequested`, `exportRequested` and `finishRequested` with owned captured
input and explicit export options. These requests perform no I/O or host
acceptance. The host must validate them through `prepareMergeExport()` before
publishing. Save and Finish require writable mode; Finish requires a Resolved
request. An export request can carry a read-only Draft. Registered Qt metatypes
support queued delivery. Paths and Git operations remain host responsibilities.

After a successful save, the host can call `acknowledgeSaved(captured)`. It marks
the current text and conflict states clean only if the session, exact bytes and
all conflict metadata still match the captured input. A later edit is never
cleared by a stale completion callback. Native Undo remains available; undoing a
saved choice makes the result modified again. Export alone does not acknowledge
saving, and saving a Draft does not resolve its conflicts.

**File > Open conflict editor...** opens one existing regular RESULT file.
**Edit RESULT** enables editing. **Save draft** writes a marker-preserving Draft;
with unresolved conflicts it asks for confirmation. **Save resolved** requires
all editor conflicts resolved and explicit review when no authoritative host list
is available. **Save resolved and close** closes only after a verified successful
Resolved save. Ctrl+S chooses Draft when conflicts remain and Resolved otherwise.
Closing unsaved content offers Save / Discard / Cancel. A successfully saved Draft
may be closed while remaining unresolved. The demo does not stage anything or
continue a repository operation.

The desktop saves only the original RESULT path. It validates SHA-256 content,
size, modification time, permissions, canonical path and, on Unix, device/inode
against the load or last successful save. A changed, replaced, removed, read-only
or symlink target blocks saving and leaves edits in memory. Reload and review the
file in a new conflict editor before retrying. Draft serialization errors also
leave the original file intact. No Save As, filesystem deletion, file-kind or mode
changes are performed by this desktop helper; an embedded host handles those
explicit export decisions separately.

Loading, export validation and saving run on workers. Save shows progress and a
Cancel save button; closing during saving requests cancellation and leaves the
window open until the worker finishes. Edits and controls are temporarily disabled
during saving. `QSaveFile` writes a temporary file, retains permissions, disables
direct-write fallback and atomically replaces the target after a second stale-file
check. The committed bytes are verified before acknowledging the save. Cancellation
after a successful commit cannot undo that replacement. Concurrent writers can
still act between the final check and atomic rename; this is not a filesystem
compare-and-swap or an exclusive lock against other processes.

The filesystem helper belongs to the desktop target, not `DiffMerge::Widgets`;
buffer-only hosts require neither a disk path nor `qt-extra`. Standalone
`--merge` role arguments and `git mergetool` completion exit status remain the
next stage; this window must not yet be configured as a trusted mergetool.

### Presentation API

`FileDiffWidget::viewMode()` and `unchangedLinesSkipped()` report the current
settings; `viewModeChanged` and `unchangedLinesSkippedChanged` notify the host.
`contextLines()` defaults to 3; negative values passed to `setContextLines()`
are clamped to zero. Widgets do not create shortcuts or save preferences.
`unifiedEditor()` exposes the unified pane for the same optional customization
as `leftEditor()` and `rightEditor()`. All public reveal/search coordinates are
still original UTF-16 file coordinates, regardless of the displayed mode.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The default configuration registers nine test suites:

| Suite | Coverage |
| --- | --- |
| `test_engine` | Diff engine and slider heuristics |
| `test_interner` | Line interning and normalization |
| `test_aligned_model` | Shared block ranges, empty-side boundaries and aligned rows |
| `test_scroll_sync_mapper` | Scroll position mapping |
| `test_intra_line_diff` | Character ranges within replacements |
| `test_diff_editor` | Change colors, boundaries, connectors and viewport updates |
| `test_cli` | CLI integration through QProcess |
| `test_slider_parser` | Corpus metadata parsing |
| `test_slider_eval` | Evaluation logic |

The tests run locally without downloading the corpus. For a headless environment,
set `QT_QPA_PLATFORM=offscreen` if a Qt platform plugin requires a display.

## Corpus tools and slider heuristics

`corpus-dl` downloads file pairs described by a local `diff-slider-tools` corpus.
`corpus-annotate` adds system diff positions to its metadata. `slider-eval`
compares diffmerge's placement of ambiguous insertion/deletion blocks with the
human choices in the corpus. Use each tool's `--help` for its options.

```bash
./build/diffmerge-corpus-dl/corpus-dl \
  --corpus /path/to/diff-slider-tools/corpus --output corpusDiff
./build/diffmerge-slider-eval/slider-eval --corpus corpusDiff
./build/diffmerge-slider-eval/slider-eval --corpus corpusDiff --heuristics
```

The following figures are recorded results from an earlier evaluation on roughly
5,400 sliders; they have not been remeasured for the current revision. The chart
shows the recorded shift bins. A shift of zero matches the human choice.

```text
Without heuristics
    -1       1  #
     0    1741  #############
     1    3392  ########################
     2     208  ##
     3      50  #
     4      20  #

With heuristics
    -1     104  #
     0    4970  ####################################
     1     173  ##
     2     102  #
     3      31  #
     4       7  #
```

The recorded match rate increased from approximately 32% to 91%.

## Repository layout

- `libdiffcore/` — public `diffcore` API, engine, normalization and heuristics.
- `diffmerge-cli/` — option parsing, file loading, hunk output and CLI tests.
- `diffmerge-gui/` — qcodeedit integration, file and directory views, scroll
  mapping, intra-line highlighting and GUI helpers.
- `diffmerge-corpus-dl/` — corpus download, parsing and annotation.
- `diffmerge-slider-eval/` — slider placement evaluation.

## Planned work

- Overview minimap.
- Unified/context CLI output and recursive CLI comparison.
- Optional three-way merge.
- Platform packaging.
