# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

NeraChess is a C++23 chess engine, UCI executable, and SDL2/Dear ImGui desktop
application, plus a Python/PyTorch pipeline that trains its NNUE evaluation
network. Search and rules code never link against another engine; Stockfish is
only ever invoked as an external process to label self-play training data, the
way a human analyst would give a position a score.

## Build (Linux headless engine — no SDL needed)

```sh
bash ./scripts/Setup-Linux.sh gmake
make -C NeraChessEngine config=release
make -C NeraChessNNUE config=release
make -C NeraChessSearch config=release
make -C NeraChessUCI config=release
make -C NeraChessSelfPlay config=release
make -C NeraChessTests config=release
```

Use `config=debug` while developing engine/search/rules code — asserts (e.g.
accumulator full-refresh verification) are compiled in.

The desktop app (`NeraChessApp`, requires SDL2) and full workspace generation
are covered per-platform in `README.md` (`scripts/Setup-macOS.sh`,
`scripts/Setup-Windows.bat`). On macOS, SDL deps come from Homebrew
(`brew install premake sdl2-compat sdl2_image sdl2_mixer`), not the
vendored Windows binaries.

## Tests and benchmarks

Run after every engine, rules, or search change:

```sh
./bin/Release/NeraChessTests/NeraChessTests
```

This is a single monolithic binary (all cases live in
`NeraChessTests/src/main.cpp`) covering perft, make/undo and hash invariants,
draw rules, FEN validation, transposition table behavior, NNUE format and
accumulator invariants, tactical search choices, time management, and the
opening-book index. There is no filter flag to run a single case — add a
regression test to `main.cpp` itself, preferring an established perft position
or a minimal isolating FEN, and keep it deterministic and network-free.

Benchmarks (same binary, separate flags):

```sh
./bin/Release/NeraChessTests/NeraChessTests --bench           # perft/search
./bin/Release/NeraChessTests/NeraChessTests --search-bench
./bin/Release/NeraChessTests/NeraChessTests --thread-bench    # scheduling-sensitive, not an Elo measurement
./bin/Release/NeraChessTests/NeraChessTests --nnue-bench      # incremental accumulator vs. full refresh
```

Utility flags on the same binary:

```sh
./bin/Release/NeraChessTests/NeraChessTests --write-random-network /tmp/random.nnue
./bin/Release/NeraChessTests/NeraChessTests --nnue-feature-vectors > NNUETraining/tests/feature_vectors.json
```

NNUE training pipeline's own suite (no dependencies needed):

```sh
cd NNUETraining && python3 -m unittest discover -s tests -t .
```

UCI smoke check for pondering:

```sh
python3 scripts/Test-UciPonder.py
```

Desktop app smoke test (SDL init, resource loading, clean shutdown, no game loop):

```sh
./bin/Debug/NeraChessApp/NeraChessApp --smoke-test
```

Format before committing:

```sh
find ApplicationCore/src NeraChessApp/src NeraChessEngine/src NeraChessSearch/src NeraChessTests/src NeraChessUCI/src \
  -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i
```

## Architecture

Layered, one Premake project per layer (`Build-<Name>.lua`, included from the
root `Build-NeraChess.lua`); each depends only on the layers below it, never
sideways or up:

- **`NeraChessEngine`** — board state, legal move generation, Zobrist hashing,
  clocks, repetition table. Pure rules, no search or eval.
- **`NeraChessNNUE`** — NNUE network format, feature indexing, per-ply
  accumulator (incremental update, verified against full refresh in Debug),
  scalar/SSE2/AVX2/NEON inference kernels that must agree bit-for-bit for
  deterministic multithreaded search.
- **`NeraChessSearch`** — PVS + iterative deepening + Lazy SMP, transposition
  table, move ordering (history/killer/countermove), time management, opening
  book, and the `Evaluation` facade. Search code reaches NNUE only through this
  facade — it never includes NNUE headers directly; preserve that boundary
  when touching either layer.
- **`NeraChessUCI`** — headless async UCI protocol adapter over Search/Engine.
- **`ApplicationCore`** — generic SDL/ImGui app framework (window, renderer,
  sound, layers, events) with no chess-specific knowledge.
- **`NeraChessApp`** — the desktop bot: `ChessPlayers/` adapters (`Human`,
  `NeraChessBot`, `BotRandom`) plug into layers built on `ApplicationCore`
  (`BoardLayer`, `GameManagerLayer`, `UILayer`, `BackgroundLayer`).
- **`NeraChessSelfPlay`** — generates NNUE training data by having the engine
  play itself (`--mode material` for the generation-0 bootstrap using piece
  values only, since generation 0 has no network yet).
- **`NeraChessTests`** — single-binary regression + benchmark suite for
  everything above.
- **`NNUETraining`** — Python/PyTorch pipeline (outside the C++ build) that
  turns self-play data into a `.nnue` file:
  - `architecture.py` / `features.py` / `serialize.py` **deliberately mirror**
    `NetworkArchitecture.h` / `FeatureSet.cpp` / `NetworkFormat.h` in C++
    (duplicated because the engine needs them at compile time). This
    duplication is cross-checked by tests, not assumed correct — if you change
    the network shape or feature indexing in C++, update the Python mirror and
    regenerate `tests/feature_vectors.json` (command above), or
    `test_architecture.py` / `test_features.py` will catch the drift.
  - Only `model.py`, `loss.py`, `train.py` need PyTorch; everything else
    (format read/write, verification, feature indexing) runs on the standard
    library so it stays testable without a heavyweight install.
  - `scripts/pipeline.py` runs the full loop (generate → train → verify) per
    generation; `nnue_training.verify` cross-checks the trainer's reference
    forward pass against the built engine's `eval` output position by
    position — a disagreement means the trainer and engine do not implement
    the same network, and no amount of training fixes that.

## Working with self-play generations

Recent history on `main` is a sequence of commits shipping successive trained
networks ("Ship generation N of the self-play network") alongside engine
changes. Generation networks live under `NeraChessApp/Resources/NNUE/`, are
loaded automatically by both the UCI engine and the desktop app at startup,
and are read by the `EvalFile` UCI option.

## Notes specific to this repo

- Default UCI search is single-threaded for reproducible engine matches;
  `Threads` is a UCI option, not a compile-time choice.
- Keep NEON/SSE2/AVX2/scalar NNUE kernels bit-for-bit identical — this is what
  keeps multithreaded search deterministic, and is checked in Debug builds.
- `gcc`/`clang` builds treat warnings as fatal for `NeraChessEngine` (see its
  `Build-NeraChessEngine.lua`); keep new code warning-clean under `-Wextra`.
- PRs are expected to pass every GitHub Actions job; changes to move
  generation or evaluation strength should be backed by a perft count,
  benchmark, or game sample rather than an estimated rating.
