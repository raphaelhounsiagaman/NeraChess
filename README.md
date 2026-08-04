# NeraChess

![NeraChess Screenshot](<assets/screenshots/NeraChessUIStartingPosition.png>)

NeraChess is a chess engine written in modern C++.  
It consists of classical search techniques.

The project started as a personal learning project and gradually evolved into a fully working engine. It is not meant to compete with top engines like Stockfish or Leela, but to explore how far a self-written engine can go.

The current engine has not yet been recalibrated in a statistically meaningful
engine tournament, so the repository does not claim an Elo rating.

---

## Project Goals

The main goals of this project are:

- Learn C++ in depth (architecture, performance, memory handling)
- Understand how chess engines work internally
- Build something complete and usable from scratch


---

## Current Features

### Timed games

- Selectable `1 min`, `3 min + 2 sec`, `10 min`, `15 min + 10 sec`, and
  `90 min + 40 sec` time controls
- Independent White and Black clocks with increment after each completed move
- Live active-player highlighting, sub-ten-second tenths, and flag-fall results
- Search budgets derived from the bot's actual remaining time and increment

### Search

- Principal Variation Search (PVS)
- Iterative Deepening
- Aspiration windows and mate-distance pruning
- Alpha-Beta Pruning
- Clustered transposition table with configurable size
- Late Move Reductions (LMR)
- Null-move, futility, delta, and static-exchange pruning
- Quiescence Search
- Killer Moves
- Side-aware history, killer, and countermove heuristics
- Tapered piece-square, pawn-structure, mobility, and king-safety evaluation
- Indexed opening book with transposition-aware lookup
- Clock-aware time management
- UCI protocol support

The search is functional and reasonably optimized, but not heavily micro-optimized compared to professional engines.

---

## Architecture

NeraChess follows a layered architecture inspired by the application structure shown in The Cherno’s C++ Application Architecture series on YouTube.

The goal of this structure is separation of concerns rather than extreme abstraction. The project is divided into logical layers with clear responsibilities:

- `NeraChessEngine`: board state, legal move generation, hashing, clocks, and rules
- `NeraChessSearch`: evaluation, search, transposition table, time management, and opening book
- `NeraChessUCI`: headless asynchronous UCI protocol adapter
- `NeraChessApp`: SDL/ImGui desktop application and chess-player adapters
- `NeraChessTests`: headless perft, state, search, tactical, book, and benchmark coverage

---

## Dependencies

### Included in Repository

- Dear ImGui
- SDL2 headers and Windows libraries
- Premake for Windows and Linux project generation

### macOS dependencies

The macOS build uses native Homebrew libraries instead of the checked-in
Windows binaries:

```sh
brew install premake sdl2-compat sdl2_image sdl2_mixer
```

---

## Build

NeraChess requires a compiler and standard library with C++23 `std::format`
and `std::print` support.

### macOS

Requirements:

- Apple Silicon or Intel Mac
- Xcode and the Xcode Command Line Tools
- The Homebrew dependencies listed above

Generate an Xcode workspace from the repository root:

```sh
./scripts/Setup-macOS.sh
open NeraChess.xcworkspace
```

Select the `NeraChessApp` scheme and build or run the Debug or Release
configuration.

For a terminal build, generate GNU Makefiles instead:

```sh
./scripts/Setup-macOS.sh gmake
make config=release
./bin/Release/NeraChessApp/NeraChessApp
```

The headless UCI engine can be launched directly or configured in any
UCI-compatible chess GUI:

```sh
./bin/Release/NeraChessUCI/NeraChessUCI
```

It supports standard position setup, `go` depth/node/time limits,
`searchmoves`, asynchronous `stop`, hash sizing and clearing, and iterative
`info` output.

UCI clocks are supplied by the controlling chess GUI on each search rather
than stored by the engine process. NeraChess supports the standard `wtime`,
`btime`, `winc`, `binc`, and `movestogo` fields, for example:

```text
position startpos moves e2e4 e7e5
go wtime 178400 btime 179100 winc 2000 binc 2000 movestogo 38
```

All UCI clock values are milliseconds. NeraChess selects the clock belonging
to the side to move, preserves a flag-fall reserve, and returns before its hard
time budget while still using iterative-deepening results from completed
depths.

Set `NERACHESS_MACOS_DEPENDENCY_PREFIX` before project generation only when
the SDL libraries are installed under a prefix other than `brew --prefix`.

### Windows

Requirements:

- Visual Studio 2026 with the Desktop development with C++ workload, or a
  recent Visual Studio 2022 installation with C++23 library support

Generate the default Visual Studio 2026 solution from any working directory:

```bat
scripts\Setup-Windows.bat
```

To generate Visual Studio 2022 files instead:

```bat
scripts\Setup-Windows.bat vs2022
```

Open `NeraChess.slnx` for Visual Studio 2026 or `NeraChess.sln` for Visual
Studio 2022, select Debug or Release, and build `NeraChessApp`.

### Linux headless engine

The bundled Linux Premake executable can generate Makefiles for the engine,
UCI target, and regression suite without SDL:

```sh
bash ./scripts/Setup-Linux.sh gmake
make -C NeraChessEngine config=release
make -C NeraChessSearch config=release
make -C NeraChessUCI config=release
make -C NeraChessTests config=release
./bin/Release/NeraChessTests/NeraChessTests
./bin/Release/NeraChessUCI/NeraChessUCI
```

The macOS and Windows GUI builds copy the `Ressources` directory beside the
executable. The application resolves assets relative to its executable, so it
can be launched from any working directory.

To verify SDL initialization, resource loading, and clean game-thread shutdown
without entering the application loop, run a built executable with:

```sh
./bin/Debug/NeraChessApp/NeraChessApp --smoke-test
```

## Verification and benchmarks

The normal regression suite includes 17 reference perft positions, make/undo
and hash invariants, draw rules, FEN validation, transposition-table behavior,
evaluation symmetry, tactical search choices, time management, and the full
opening-book index:

```sh
./bin/Release/NeraChessTests/NeraChessTests
```

Two deterministic benchmark modes are also available:

```sh
./bin/Release/NeraChessTests/NeraChessTests --bench
./bin/Release/NeraChessTests/NeraChessTests --search-bench
```

---

## Known Limitations

- No multi-threaded search.
- Evaluation parameters are hand-tuned rather than trained from games.

---

## License

MIT License as in LICENSE.TXT
