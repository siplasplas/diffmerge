# DiffMerge — v0.2.1 (etap 1 + CLI + etap 2: DiffEditor read-only)

Narzędzie do porównywania plików tekstowych. Składa się z:

- **`libdiffcore`** — biblioteka silnika diff (szablonowany O(NP) Wu/Manber/Myers)
- **`diffmerge`** (CLI) — smoke CLI do ręcznego testowania
- **`diffmerge-gui`** (GUI, etap 2) — dwupanelowy widok w stylu JetBrains,
  read-only

## Zmiany w v0.2.1

- Fix: scrollbar pionowy nie zasłania już guttera / side margin.
  Marginesy są przesuwane w lewo o szerokość scrollbara, gdy ten staje
  się widoczny (reaguje na `rangeChanged`).

## Wymagania

- CMake ≥ 3.21
- Qt 6 (moduły Core, Gui, Widgets, Test)
- Kompilator C++20 (GCC 10+, Clang 11+, MSVC 19.29+)

### Linux (Ubuntu/Debian)
```bash
sudo apt install cmake g++ qt6-base-dev qt6-base-dev-tools
```

### MSYS2 UCRT64 (Windows)
```bash
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-qt6-base
```

## Budowanie

```bash
cd diffmerge
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j
```

Powstanie:
- `libdiffcore/libdiffcore.a` — biblioteka statyczna
- `diffmerge-cli/diffmerge` — CLI
- `diffmerge-gui/diffmerge-gui` — GUI
- 4 binarki testowe w `tests/`

## Uruchomienie testów

```bash
cd build
ctest --output-on-failure
```

Oczekiwany wynik: **4 test suites, 48 tests passed**.

- `test_engine` — 17 testów (silnik)
- `test_interner` — 9 testów (normalizacja)
- `test_aligned_model` — 8 testów (odwzorowanie linii na widok)
- `test_cli` — 14 testów integracyjnych (QProcess)

## GUI (etap 2)

```bash
# Z linii komend (otworzy od razu w widoku diff)
./build/diffmerge-gui/diffmerge-gui fileA.txt fileB.txt

# Interaktywnie (File > Open two files)
./build/diffmerge-gui/diffmerge-gui
```

### Co robi

- Dwupanelowy widok side-by-side, **read-only** (edycja w etapie 4)
- **Numery linii po zewnętrznych krawędziach** obu edytorów
  (JetBrains-style — lewy edytor ma numery po lewej, prawy po prawej)
- **Pasek zmian po wewnętrznych krawędziach** (kolor zależny od typu
  zmiany — zielony Insert, czerwony Delete, niebieski Replace)
- **Podświetlenia tła całych linii** w odpowiednim kolorze
- **Placeholdery "— — —"** w miejscach gdzie po drugiej stronie jest
  linia której nie ma po tej — dzięki temu odpowiadające hunki są na
  tym samym Y po obu stronach
- **Numery linii pomijają placeholdery** — gutter pokazuje rzeczywisty
  numer w pliku, nie numer wiersza widoku
- **Auto-detekcja jasny/ciemny motyw** — kolory adaptują się do
  `QPalette::Window`

### Przykładowy screenshot

Wyrenderowany z `code_a.cpp` vs `code_b.cpp`:
- Funkcja `int multiply(...)` dodana w prawej → zielone tło + 5 kreseczek
  po lewej
- `std::cout << "sum: "` → niebieska linia (Replace)
- Numery linii po lewej: 1-12, po prawej: 1-18

### Znane ograniczenia (etap 2)

- **Brak sync scroll** — każdy edytor przewija się niezależnie (etap 3)
- **Brak nawigacji prev/next change** — etap 3
- **Brak overview minimap** po prawej — etap 3
- **Brak word-level diff wewnątrz Replace** — etap 4
- **Read-only** — edycja i "apply hunk" to etap 4

## CLI

Patrz wcześniejsza dokumentacja: `diffmerge fileA.txt fileB.txt`,
opcje `-i`, `-w`, `-b`, `--brief`, `--color`, `--help`.
Pełny CLI z unified-diff formatem to etap 6.

## Opcje CMake

```bash
cmake -DDIFFMERGE_BUILD_GUI=OFF ..     # Tylko biblioteka + CLI
cmake -DDIFFMERGE_BUILD_CLI=OFF ..     # Tylko biblioteka + GUI
cmake -DDIFFMERGE_BUILD_TESTS=OFF ..   # Bez testów
```

## Struktura

```
diffmerge/
├── CMakeLists.txt                       Top-level
├── libdiffcore/
│   ├── include/diffcore/                Publiczne API
│   │   ├── DiffTypes.h                  Hunk, LineRange, DiffResult, DiffOptions
│   │   ├── LineInterner.h               Mapowanie linii → int ID
│   │   └── DiffEngine.h                 compute(left, right) → DiffResult
│   ├── src/
│   │   ├── LineInterner.cpp
│   │   ├── DiffEngine.cpp               Adapter (7 helperów)
│   │   └── internal/                    Wewnętrzne
│   │       ├── Diff.h                   Szablonowany silnik O(NP)
│   │       └── Structs.h
│   └── tests/                           17 + 9 testów
├── diffmerge-cli/
│   ├── src/                             main + CliOptions + FileLoader + HunkPrinter
│   └── tests/                           14 testów + fixtures
└── diffmerge-gui/
    ├── src/
    │   ├── main.cpp
    │   ├── MainWindow.{h,cpp}           Okno z menu File > Open
    │   ├── common/
    │   │   └── ColorScheme.{h,cpp}      Light/dark auto-detect
    │   ├── editor/
    │   │   ├── DiffEditor.{h,cpp}       QPlainTextEdit + marginesy + tło
    │   │   ├── DiffGutter.{h,cpp}       Numery linii (pomija placeholdery)
    │   │   └── DiffSideMargin.{h,cpp}   Pasek zmian (kolor)
    │   └── fileview/
    │       ├── AlignedLineModel.{h,cpp} ⭐ Odwzorowanie linii → widok
    │       └── FileDiffWidget.{h,cpp}   Kontener z 2 edytorami
    └── tests/
        ├── test_aligned_model.cpp       8 testów
        └── screenshot_tool.cpp          Narzędzie dev (offscreen PNG)
```

## Roadmapa

- [x] **Etap 1** — silnik + adapter + CLI smoke + testy
- [x] **Etap 2** — DiffEditor read-only + AlignedLineModel
- [ ] **Etap 3** — sync scroll + overview minimap + nawigacja prev/next
- [ ] **Etap 4** — apply hunk ← → + recompute on edit + word-level diff
- [ ] **Etap 5** — porównanie katalogów (DirDiffWidget)
- [ ] **Etap 6** — pełny CLI (unified, context, recursive)
- [ ] **Etap 7** — (opcja) 3-way merge
- [ ] **Etap 8** — packaging (.deb, .rpm, MSYS2)
