# DiffMerge

DiffMerge compares text files through a Qt 6 desktop application and a command-line
interface. Both use `libdiffcore`, a line diff engine based on the O(NP)
Wu/Manber/Myers algorithm, with normalization options and slider placement
heuristics.

The GUI currently provides read-only comparison. Editing and merging are planned.

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
- Qt 6.2 or newer: Core for the library and CLI; Gui, Widgets and Svg for the GUI;
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
views. Passing a single path pre-fills the left path field.

File comparison provides:

- Two read-only qcodeedit panes with line numbers on the inner edges and vertical
  scrollbars on the outer edges.
- Synchronized scrolling across corresponding changes.
- Previous/next change navigation with **Shift+F7** and **F7**, or toolbar buttons.
- Three-level comparison: line alignment, word matching within replacement
  blocks, then character comparison within corresponding changed words.
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

Directory comparison shows a tree with `same`, `different`, `only left` and
`only right` statuses, and recurses into directories present on both sides.
Activate a file present on both sides to open its text comparison; use
**Directories** to return to the tree.

### Syntax highlighting

File comparisons select Kate XML syntax definitions independently for each side
using the file name. Syntax foreground colors and font styles remain visible over
the existing diff block and character backgrounds. Comments and strings retain
their syntax state across original document lines. Colors adapt to the light or
dark diff scheme. Folding stays disabled, including syntax fold markers.

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

### Current limitations

- Files cannot be edited and changes cannot be applied between panes.
- There is no overview minimap or three-way merge.
- The editor displays real document lines without visual filler rows, so panes
  can have different heights around insertions and deletions.
- Directory file statuses use size and modification time, rather than a content
  comparison. Names are matched without regard to case and hidden entries are
  excluded. Files present on only one side cannot be opened from the tree.

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
Splitting or joining words by changing only spacing highlights that spacing.

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
| `DiffMerge::Widgets` | File and directory comparison widgets | Core, Qt6 Widgets/Svg, qcodeedit + qcodeedit-kate >= 1.6.0 |

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
find_package(DiffMerge 1.1 CONFIG REQUIRED COMPONENTS Widgets)
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
shows this with QtConcurrent; DiffMerge itself does not require QtConcurrent.

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
the comparison widget does not display a metadata row or insert synthetic content.
End-of-line differences do not create textual change
blocks. Binary detection, decoding, repository access and revision identity belong
to the host.

`PrepareResult::preparationTime` and `lastInstallationTime()` report computation
and GUI installation separately. GUI document installation and stored aligned
rows/highlights still consume time and memory; this implementation does not
virtualize large documents.

One local Release-build measurement (Linux, Qt 6.10, offscreen, one run) requested
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
run, not hard latency or memory guarantees.

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

- File editing, applying changes between panes and recomputing differences.
- Overview minimap.
- Unified/context CLI output and recursive CLI comparison.
- Optional three-way merge.
- Platform packaging.
