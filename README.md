# NeraChess

![NeraChess Screenshot](<assets/screenshots/NeraChessUIStartingPosition.png>)

NeraChess is a chess engine written in modern C++.  
It consists of classical search techniques.

The project started as a personal learning project and gradually evolved into a fully working engine. It is not meant to compete with top engines like Stockfish or Leela, but to explore how far a self-written engine can go.

Current estimated strength: roughly beginner to intermediate club level (~1200–1400 Elo, depending on time control).

---

## Project Goals

The main goals of this project are:

- Learn C++ in depth (architecture, performance, memory handling)
- Understand how chess engines work internally
- Build something complete and usable from scratch


---

## Current Features

### Search

- Principal Variation Search (PVS)
- Iterative Deepening
- Alpha-Beta Pruning
- Clustered transposition table with configurable size
- Late Move Reductions (LMR)
- Null-move, futility, delta, and static-exchange pruning
- Quiescence Search
- Killer Moves
- History Heuristic
- Tapered piece-square, pawn-structure, mobility, and king-safety evaluation
- Indexed opening book with transposition-aware lookup
- Clock-aware time management
- UCI protocol support

The search is functional and reasonably optimized, but not heavily micro-optimized compared to professional engines.

---

## Architecture

NeraChess follows a layered architecture inspired by the application structure shown in The Cherno’s C++ Application Architecture series on YouTube.

The goal of this structure is separation of concerns rather than extreme abstraction. The project is divided into logical layers with clear responsibilities:

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

---

## Known Limitations

- No multi-threaded search.
- Evaluation parameters are hand-tuned rather than trained from games.

---

## License

MIT License as in LICENSE.TXT
