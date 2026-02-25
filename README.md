# NeraChess

![NeraChess Screenshot](<assets/screenshots/NeraChessUIStartingPosition.png>)

NeraChess is a chess engine written in modern C++.  
It combines classical search techniques with neural network evaluation using ONNX Runtime and CUDA.

The project started as a personal learning project and gradually evolved into a fully working engine. It is not meant to compete with top engines like Stockfish or Leela, but to explore how far a self-written engine can go using hybrid classical + neural approaches.

Current estimated strength: roughly beginner to intermediate club level (~1200–1400 Elo, depending on time control).

---

## Project Goals

The main goals of this project are:

- Learn C++ in depth (architecture, performance, memory handling)
- Understand how chess engines work internally
- Experiment with combining alpha-beta search and neural evaluation
- Build something complete and usable from scratch

It is a long-term personal project and still under active development.

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

### Evaluation

- Neural network evaluation via ONNX Runtime
- CUDA + cuDNN acceleration
- Multiple trained networks included
- Custom 19-channel board representation
- Models trained externally (PyTorch) and exported to ONNX

The neural network replaces a classical handcrafted evaluation.  
However, evaluation latency is still a limiting factor. Larger networks reduce search depth significantly, so smaller models currently perform better overall.

NNUE-style incremental evaluation is being developed but is **not yet integrated**.

---

## Architecture

NeraChess follows a layered architecture inspired by the application structure shown in The Cherno’s C++ Application Architecture series on YouTube.

The goal of this structure is separation of concerns rather than extreme abstraction. The project is divided into logical layers with clear responsibilities:

---

### Board Layer

The `BoardLayer` is responsible for rendering and user interaction.

It handles:

- Drawing the chessboard and pieces
- Loading and mapping piece sprites from a texture atlas
- Handling mouse input (click, drag, release)
- Converting screen coordinates to board squares
- Highlighting selected squares and last moves
- Playing move and capture sounds
- Animating piece movement
- Resizing and recalculating board layout dynamically

The layer maintains its own visual copy of the board state and updates it when moves are received from the `GameManagerLayer`. Move animations are time-based and executed in the update loop to keep rendering smooth and independent from engine timing.

User moves are validated against the current legal move list before being forwarded to the game manager. This ensures that illegal moves are filtered at the UI level without affecting the core engine.

The `BoardLayer` contains no search logic and does not evaluate positions. Its sole responsibility is presentation and interaction.

---

### GameManager Layer

The `GameManagerLayer` coordinates the actual gameplay loop between the engine and the UI.

It is responsible for:

- Initializing and resetting games
- Managing the two player instances (human or bot)
- Running the main game loop on a separate thread
- Validating moves before applying them
- Detecting game-over conditions
- Synchronizing engine moves with the UI via a thread-safe move queue

The game logic runs in a detached background thread to prevent the UI from blocking during move calculation. Computed moves are pushed into a queue and applied in the main update loop, where the `BoardLayer` is notified to update the visual state.

This layer acts as a bridge between the chess core (board + search) and the application layer, ensuring clean separation between engine logic and rendering.

---

### UI Layer

The `UILayer` provides a minimal control interface using ImGui.

It is responsible for:

- Rendering simple control elements (e.g. start/stop buttons)
- Forwarding user commands to the `GameManagerLayer`

This layer does not contain any game logic, rendering logic, or engine functionality. It only acts as a lightweight control panel for interacting with the application state.

The UI is intentionally simple and exists primarily for development and testing purposes rather than as a full-featured interface.

---

## Dependencies

### External (required)

- ONNX Runtime (GPU version recommended)
- CUDA Toolkit
- cuDNN

### Included in Repository

- SDL
- myGUI
- Premake (for generating build files)

---

## Build (Windows)

Currently tested on Windows only.

### Requirements

-  A C++ compiler supporting C++23 (tested with MSVC / Visual Studio 2022)
- CUDA Toolkit
- cuDNN
- ONNX Runtime GPU (version: `onnxruntime-win-x64-gpu-1.24.2`)

---

### 1. Install CUDA and cuDNN

Install the CUDA Toolkit and cuDNN matching your GPU and driver version.  
Make sure CUDA is properly added to your system `PATH`.

---

### 2. Download ONNX Runtime (GPU)

Download the following package from the official ONNX Runtime GitHub releases page:

```
onnxruntime-win-x64-gpu-1.24.2
```

https://github.com/microsoft/onnxruntime/releases/download/v1.24.2/onnxruntime-win-x64-gpu-1.24.2.zip

Extract the archive into:

```
NeraChessApp/vendor
```

The extracted folder should contain at least:

```
include/
lib/
```

---

### 3. Generate Project Files

Run the script located at:

```
scripts/Setup-Windows.bat
```

to create Visual Studio 2022 files.

---

### 4. Build

Open the generated solution in Visual Studio and build in Release mode.

---

## Known Limitations

- Neural evaluation is still relatively slow compared to NNUE-style engines.
- No UCI support yet.
- No multi-threaded search.
- Network switching requires recompilation.
- Limited benchmarking infrastructure.

---

## Roadmap

- Integrate NNUE-style evaluation
- Add UCI compatibility
- Add multi-threaded search
- Improve benchmarking and testing

---

## License

MIT License as in LICENSE.TXT