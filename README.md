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
- Transposition Table (256 MB)
- Late Move Reductions (LMR)
- Quiescence Search
- Killer Moves
- History Heuristic
- Opening Book support
- Basic time management

The search is functional and reasonably optimized, but not heavily micro-optimized compared to professional engines.

---

## Architecture

NeraChess follows a layered architecture inspired by the application structure shown in The Cherno’s C++ Application Architecture series on YouTube.

The goal of this structure is separation of concerns rather than extreme abstraction. The project is divided into logical layers with clear responsibilities:

---

## Dependencies

### Included in Repository

- SDL
- myGUI
- Premake (for generating build files)

---

## Build (Windows)

Currently tested on Windows only.

### Requirements

-  A C++ compiler supporting C++23 (tested with MSVC / Visual Studio 2022)

---

### 1. Generate Project Files

Run the script located at:

```
scripts/Setup-Windows.bat
```

to create Visual Studio 2026 files.

---

### 2. Build

Open the generated solution in Visual Studio and build in Release mode.

---

## Known Limitations

- No UCI support yet.
- No multi-threaded search.
- Network switching requires recompilation.
- Limited benchmarking infrastructure.

---

## License

MIT License as in LICENSE.TXT