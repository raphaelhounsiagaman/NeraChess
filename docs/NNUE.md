# NNUE

This document describes the NNUE evaluation being built on this branch: the
architecture, the pieces that exist, the pieces that do not, and the order they
should be built in.

> **Status.** Inference and the self-play training loop both work end to end,
> and a trained network ships at `NeraChessApp/Resources/NNUE/nera.nnue`, so a
> fresh clone plays out of the box. It came from 31 self-play generations at
> shallow depth and no external engine was involved at any point. It plays real
> chess but is well short of the hand-crafted evaluation it replaced; see
> [Training by self-play](#training-by-self-play) to train a stronger one.

---

## What NNUE is

A classical evaluation is a human-written function: material, piece-square
tables, mobility, king safety, pawn structure. Every term is a guess about what
matters, hand-tuned against games.

NNUE replaces that function with a small neural network trained on labelled
positions. The "efficiently updatable" part is what makes it viable inside a
search: the first layer is a plain sum of one weight column per active input
feature, so a move that changes three features can adjust the network's hidden
layer with three column operations instead of recomputing 768 x 512 products.
The expensive layer is only recomputed when the search makes or unmakes a move,
and only for what actually changed.

---

## Architecture

```
(768 -> 512)x2 -> 1
```

| Property | Value | Why |
| --- | --- | --- |
| Features per perspective | 768 | `(relative colour, piece type, relative square)` = 2 x 6 x 64 |
| Input buckets | 1 | King-independent, so a king move never forces a refresh |
| Hidden size | 512 per perspective | 1024 values reach the output layer |
| Output buckets | 1 | One head for every position |
| Activation | Squared clipped ReLU | `clamp(x, 0, 1)^2` |
| Quantization | QA 255, QB 64 | Feature weights at 255, output weights at 64 |
| Eval scale | 400 | Maps the network output onto centipawns |

The single source of truth is
[`NeraChessNNUE/src/NetworkArchitecture.h`](../NeraChessNNUE/src/NetworkArchitecture.h),
mirrored in
[`NNUETraining/nnue_training/architecture.py`](../NNUETraining/nnue_training/architecture.py).

### Why this shape

It is the smallest architecture worth training. A perspective network with no
king bucketing gets most of the strength of a full HalfKP or HalfKA network
while being dramatically simpler: no refresh logic, no bucket transitions, and
a feature index that fits in three lines of arithmetic. Getting a weak network
playing end to end is worth more right now than getting a strong architecture
half-built.

The bucket dimensions already exist in the code with a count of 1, so adding
king buckets or material-based output heads later changes `FeatureSet::KingBucket`
and `Network::OutputBucketOf` rather than every call site.

### Perspectives

Each side sees its own version of the position: its own pieces are "colour 0",
the opponent's are "colour 1", and Black's board is flipped vertically. Both
perspectives share one weight matrix, so a position and its colour-and-rank
mirror produce mirrored feature sets and evaluate identically. The output layer
always reads the side to move first, so the network never has to learn the same
pattern twice.

This is tested from both directions: `TestNnueFeatureIndexing` in the C++ suite
and `test_mirrored_positions_produce_mirrored_features` in the Python one.

---

## Layout

### `NeraChessNNUE` — inference

| File | Contents |
| --- | --- |
| `NnueCommon.h` | Shared types: `Weight`, `Accumulation`, `Perspective` |
| `NetworkArchitecture.h` | Every architecture constant and the architecture hash |
| `FeatureSet.{h,cpp}` | Feature indexing, active-feature collection, deltas |
| `DirtyPieces.{h,cpp}` | What a move changes, derived from the move encoding |
| `Accumulator.{h,cpp}` | The hidden layer, its refresh and update, and the per-ply stack |
| `Network.{h,cpp}` | Weight storage, loading, saving, and the forward pass |
| `NetworkFormat.{h,cpp}` | The `.nnue` container |
| `Quantization.h` | Fixed-point conventions, activation, dequantization |
| `SimdOps.h` | Vector primitives: scalar reference plus NEON, SSE2, and AVX2 |
| `NnueEvaluator.{h,cpp}` | The process-wide network and the engine's entry point |

### `NeraChessSelfPlay` — training data

Generates training data without any external engine. `--mode material` labels
random play with material balance to bootstrap generation 0; the default mode
plays games with a network and labels them with search scores and results.

### `NNUETraining` — training

See [`NNUETraining/README.md`](../NNUETraining/README.md).

### How the engine reaches it

`NeraChessSearch::Evaluation` is a facade: search never includes NNUE headers
directly, so swapping or wrapping the evaluator touches one file. `SearchEngine`
owns a per-worker `AccumulatorStack` that is pushed and popped alongside the
board's make/unmake.

---

## The network file

`.nnue` is little-endian and self-describing. A 48-byte header records the
format version, the architecture hash, every shape constant, the parameter
count, and an FNV-1a checksum of the payload; the engine rejects a file whose
architecture does not match the one it was compiled with.

That check matters more than it looks. A network loaded with the wrong shape
does not crash — it produces plausible-looking numbers that are meaningless,
which is the single most expensive class of bug in this whole subsystem.

Load it with the standard UCI option:

```text
setoption name EvalFile value /path/to/nera.nnue
```

Both binaries also discover a network at startup, so a packaged build needs no
configuration. They look for `nera.nnue` beside the executable, then in
`Resources/NNUE/` next to it — where the desktop build's resource copy lands —
then in the working directory. The desktop application checks its own resource
directory first and says on the console what it found, or that it found
nothing and will therefore play badly. The UCI `eval` command reports which
network is loaded and which SIMD kernels are compiled in.

`NeraChessApp/Resources/NNUE/nera.nnue` is the network the engine ships with,
and it is tracked in git deliberately -- `.gitignore` excludes `*.nnue` and then
un-excludes that one path. Both the desktop and the UCI builds copy it beside
their executables, so a clone plays without any configuration. Replacing the
shipped network means overwriting that file.

---

## What is done and what is not

### Done

- Architecture constants and the architecture hash, matched across C++ and Python
- Feature indexing, verified identical in both languages against a shared fixture
- The `.nnue` format: read, write, round-trip, and rejection of damaged files
- Accumulator refresh, and incremental updates via `ApplyDelta`
- `DescribeMove`, covering quiet moves, captures, en passant, promotions,
  capture-promotions, and all four castles
- The forward pass, verified integer-for-integer against the Python reference
  on real positions
- The evaluator, the `EvalFile` UCI option, and startup discovery
- **Incremental accumulator updates**, driven by the search's make/unmake pairs
- **NEON, SSE2, and AVX2 kernels**, each checked bit-for-bit against scalar
- The PyTorch model, loss, batching, and training loop
- Quantization and export
- **Self-play data generation and the training loop**, end to end and with no
  external engine

### Not done

1. **A network that is actually strong.** The one that ships plays legitimate
   chess -- it opens with 1.e4 e5 2.Nc3 Nc6 3.Nf3 Nf6 4.d4, finds mates and free
   material, and solves the kiwipete tactic -- but it misses the positional
   benchmark in `TestSearchChoices`, which is why that test still skips itself.
   More generations at greater depth is the whole answer.
2. **Score calibration.** Self-play training against game results inflates the
   scale: the shipped network calls a queen roughly +2800 rather than +900.
   Move ordering only cares about the ordering, so play is unaffected, but the
   scores a GUI displays are misleading and the search's centipawn-denominated
   pruning margins are effectively tighter than intended.
3. **Architecture growth** — king buckets, output buckets, a wider hidden layer
   — each worth A/B testing against the previous network rather than assuming.

---

## Training by self-play

NeraChess never labels positions with another engine's evaluations. That leaves
a bootstrapping problem, because training needs search scores, search needs an
evaluation, and the evaluation is the network being trained.

The circle is broken with exactly one piece of injected knowledge — piece
values, the same ones the move ordering already uses for static exchange
evaluation — and even that lives in the training data rather than in the
engine.

### Generation 0: material, learned rather than coded

`NeraChessSelfPlay --mode material` plays random legal moves and labels each
position with its material balance. Training on that yields a network that
evaluates material and nothing else:

| Position | gen0 evaluation |
| --- | --- |
| Start position | -14 |
| Up a knight | +309 |
| Up a queen | +877 |
| Down eight pawns | -778 |

Accurate to about 25cp, which is all it needs to be. The alternative — putting
a material evaluation back into C++ — would mean a second evaluation path in
the engine that has to be removed again later. This way generation 0 is a file
that gets deleted.

### Generation 1 and beyond: real games

`NeraChessSelfPlay` then plays games with the current network, labelling every
position with the search score and the eventual game result. Training on
generation 0's games produces a network that has learned things generation 0
had no concept of:

| Property | gen0 (material) | gen1 (self-play) |
| --- | --- | --- |
| Centralized knight over a rim knight | +4 | **+68** |
| Pawn on the seventh over the second | -5 | **+88** |

Nobody told it that knights belong in the centre or that passed pawns want to
advance. Those are the piece-square terms the hand-crafted evaluation used to
contain, recovered from self-play alone.

### The three things that decide whether it works

**Adjudication.** A weak network shuffles until the fifty-move rule fires, so
without it nearly every label is a draw and the data teaches nothing. Games are
adjudicated as a win when the score stays past `--win-score` for `--win-plies`
consecutive plies, and as a draw when it stays near zero late in the game. With
adjudication on, a generation-0 run produces roughly 74% decisive games; without
it, almost none.

**Opening variety.** A deterministic engine from the start position plays one
game forever. `--random-plies` plays random legal moves first, and those
positions are never recorded.

**Filtering.** A score that belongs to a tactic the search is still resolving
is not a label for the position. Positions in check, positions whose best move
is a capture or promotion, mate scores, and anything beyond `--max-score` are
all dropped, along with approximate duplicates.

### Running it

```bash
python3 NNUETraining/scripts/pipeline.py --workdir runs/first --generations 5
```

That does everything: material data, generation 0, then self-play, train, and
verify for each generation. It reuses files that already exist, so an
interrupted run resumes. `--install NeraChessApp/Resources/NNUE/nera.nnue`
copies the final network where both binaries will find it.

The stages can be run individually:

```bash
./bin/Release/NeraChessSelfPlay/NeraChessSelfPlay --mode material --output gen0.txt --positions 1000000
```

```bash
./bin/Release/NeraChessSelfPlay/NeraChessSelfPlay --network gen0.nnue --output gen1.txt --positions 2000000 --depth 6
```

### What it costs

Measured on an 8-core Apple M-series:

| Stage | Rate | For 2M positions |
| --- | --- | --- |
| Material labelling | ~850k positions/s | 2 seconds |
| Self-play, `--depth 4` | ~1600 positions/s | 21 minutes |
| Self-play, `--nodes 5000` | ~1235 positions/s | 27 minutes |
| Self-play, `--depth 6` | ~120 positions/s | 4.6 hours |
| Training, 20 epochs | ~1.5s per 200k per epoch | 5 minutes |

Self-play dominates completely — training is minutes against hours. Depth is
the expensive knob: two extra plies cost 13x. Prefer `--nodes` for a
predictable budget per move, and keep the depth low for early generations,
because position variety matters more than label precision while the network is
still weak.

Training is bound by batch collation in Python rather than by the gradient
step, so a GPU buys almost nothing at this network size — measured within 15%
of CPU on Apple MPS. Spend the hardware on self-play cores instead.

Every stage is `--seed` reproducible.

See [TRAINING.md](TRAINING.md) for the full walkthrough, including running on a
Linux server and installing the result.

---

## Incremental updates

The search never calls `MakeMove` directly. Every move goes through
`SearchEngine::MakeSearchMove` and `UndoSearchMove`, which pair the board
update with an accumulator push and pop. Routing both through one place is
deliberate: an unpaired make/unmake would leave the accumulator describing a
different position than the board, and the resulting evaluations would be
wrong in a way no test output points at.

A push copies the parent's accumulator and applies only what the move changed,
which `DescribeMove` derives from the move encoding plus the captured piece
read off the board beforehand. A pop is free — the parent's entry was never
touched. Null moves push nothing at all: no piece moves, so the accumulator
stays valid and only the side to move changes, which the output layer reads
from the board state.

Three things guard this:

* `TestNnueAccumulatorUpdates` checks each move type's delta in isolation.
* `TestNnueAccumulatorStack` walks real move trees — castling both ways, en
  passant, promotions with and without capture, rook captures that change
  castling rights — and requires the incremental accumulator to equal a full
  refresh at every node.
* `NNUE_VERIFY_ACCUMULATOR`, on by default in Debug builds, re-derives every
  accumulator the engine evaluates and asserts it matches a refresh. This
  covers the search's own paths, quiescence captures included, and it is what
  turns a subtly wrong delta into an immediate abort.

Measured with `--nnue-bench` on an Apple M-series with NEON kernels,
incremental updates evaluate roughly 5.9x faster than refreshing on dense
middlegame positions, at around 1.7M evaluations per second.

---

## SIMD

`SimdOps.h` holds the vector primitives: the three accumulator updates and the
output layer's activated dot product. Selection is at compile time, defaulting
to the widest instruction set each platform guarantees — NEON on AArch64, SSE2
on x86-64. AVX2 is used when the build enables it:

```sh
make config=release CXXFLAGS="-mavx2"
```

x86 builds without AVX2 get vector accumulator updates but a scalar output
layer. SSE2 has no packed 32-bit multiply — that arrived with SSE4.1 — and
emulating one costs more than it saves.

### Exactness is the constraint, not speed

Every kernel must produce results identical to `Simd::Scalar`, bit for bit.
This is not fastidiousness: Lazy SMP workers share a transposition table, so if
two threads evaluated the same position differently they would write
contradictory scores into it and the search would act on whichever landed last.
A network binary that scored differently on AVX2 and on NEON would also make
every reproduced game and every regression test machine-dependent.

The kernels are therefore written so that no intermediate value can overflow
for *any* int16 parameters the format permits, not merely for the small weights
a trained network happens to produce. With squared clipped ReLU, one term is
`clamp(v, 0, 255)^2 * w`: at most 65025 times 32767, which fits in int32 with
almost nothing to spare, while a sum of a thousand such terms does not. The
running total is therefore widened to int64 as it goes rather than accumulated
in int32 and widened at the end.

`TestNnueSimdKernels` compares every kernel against the scalar reference on
int16 extremes, the values either side of the activation's clipping ceiling,
and a saturated accumulator against the largest weights the format allows. The
NEON, SSE2, and AVX2 paths were each confirmed exact at `-O0`, `-O2`, and `-O3`
under both the Clang and GCC frontends.

---

## Testing

The C++ suite covers the format, feature indexing, accumulator updates against
a full refresh, and the evaluator:

```bash
./bin/Release/NeraChessTests/NeraChessTests
```

The Python suite covers the architecture hash, feature indexing against the
engine's own output, the format, quantization, and batching. It needs no
dependencies:

```bash
cd NNUETraining && python3 -m unittest discover -s tests -t .
```

A network of random weights is enough to exercise every path a real network
will. The engine can write one itself, with no Python involved:

```bash
./bin/Release/NeraChessTests/NeraChessTests --write-random-network /tmp/random.nnue
```

The end-to-end check is `nnue_training.verify`, which requires the engine and
the trainer to produce identical integers for the same position and network:

```bash
cd NNUETraining && python3 -m nnue_training.verify --network /tmp/random.nnue --engine ../bin/Release/NeraChessUCI/NeraChessUCI
```

`scripts/make_random_network.py` writes the same file from Python, and with the
same seed the two are byte-identical.

To measure the accumulator, which compares incremental updates against full
refreshes over the same move tree:

```bash
./bin/Release/NeraChessTests/NeraChessTests --nnue-bench
```

Run it on a Release build; Debug enables `NNUE_VERIFY_ACCUMULATOR`, whose
refresh-per-evaluation makes the comparison meaningless.

Tests that measure evaluation quality — `TestSearchChoices` — skip themselves
while no network is loaded and print that they did. The test binary does not
load the shipped network, and the shipped network does not yet pass those
benchmarks anyway; they are waiting on a stronger one.

To compare two networks head to head:

```bash
cd NNUETraining && .venv/bin/python scripts/match.py --a runs/first/gen30.nnue --b runs/first/gen31.nnue --games 200
```
