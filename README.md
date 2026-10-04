# DiffMerge

DiffMerge compares text files through a Qt 6 desktop application and a command-line
interface. Both use `libdiffcore`, a line diff engine based on the O(NP)
Wu/Manber/Myers algorithm, with normalization options and slider placement
heuristics.

The GUI currently provides read-only comparison. Editing and merging are planned.

## Requirements

- CMake 3.21 or newer.
- A C++20 compiler.
- Qt 6.2 or newer: Core for the library and CLI; Gui, Widgets and Svg for the GUI;
  Network for the corpus downloader; Test when building tests.
- qcodeedit 1.6.0 or newer for the GUI. CMake first looks for an installed package.
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
library is built without its demo, tests or Kate components. Disabling the GUI
removes the qcodeedit dependency.

All build options below default to `ON`:

| Option | Builds |
| --- | --- |
| `DIFFMERGE_BUILD_GUI` | Desktop application |
| `DIFFMERGE_BUILD_CLI` | Command-line file comparison |
| `DIFFMERGE_BUILD_CORPUS_DL` | Corpus download and annotation tools |
| `DIFFMERGE_BUILD_SLIDER_EVAL` | Slider evaluation tool |
| `DIFFMERGE_BUILD_TESTS` | Tests for enabled components and the core library |

For example, build only the core library and CLI, with tests:

```bash
cmake -S . -B build-cli \
  -DDIFFMERGE_BUILD_GUI=OFF \
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
- Line backgrounds for insertions, deletions and replacements, plus stronger
  character-level highlighting within replacements.
- One-sided changes use green backgrounds in either pane and a green boundary
  line on the side without a block. Replacements use blue backgrounds.
- The draggable center divider connects corresponding blocks with colored
  curves, narrowing to a line when a block exists on only one side.
- A color scheme selected from the system's light or dark palette.
- Editable path fields and file selection buttons for reloading comparisons.

Directory comparison shows a tree with `same`, `different`, `only left` and
`only right` statuses, and recurses into directories present on both sides.
Activate a file present on both sides to open its text comparison; use
**Directories** to return to the tree.

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
