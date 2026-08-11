# NNUE

This document describes the NNUE evaluation being built on this branch: the
architecture, the pieces that exist, the pieces that do not, and the order they
should be built in.

> **Status.** The scaffolding is in place and tested. No trained network
> exists, and the hand-crafted evaluation this branch replaced is gone, so
> `NeraChess` on this branch evaluates every position as `0` and plays no
> better than its search alone. That is expected until step 4 below is done.

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
| `SimdOps.h` | Vector primitives; scalar reference only so far |
| `NnueEvaluator.{h,cpp}` | The process-wide network and the engine's entry point |

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

The engine also picks up `nera.nnue` from its own directory at startup, so a
packaged build needs no configuration. The `eval` command reports which network
is loaded and which SIMD kernels are compiled in.

---

## What is done and what is not

### Done

- Architecture constants and the architecture hash, matched across C++ and Python
- Feature indexing, verified identical in both languages against a shared fixture
- The `.nnue` format: read, write, round-trip, and rejection of damaged files
- Accumulator refresh, and incremental updates via `ApplyDelta`
- `DescribeMove`, covering quiet moves, captures, en passant, promotions,
  capture-promotions, and all four castles
- The scalar forward pass, verified integer-for-integer against the Python
  reference on real positions
- The evaluator, the `EvalFile` UCI option, and startup discovery
- The per-ply accumulator stack, pushed and popped in the search
- The PyTorch model, loss, batching, and training loop
- Quantization and export

### Not done

1. **Incremental accumulator updates in the search.** `AccumulatorStack::Push`
   marks the new entry stale, so the evaluator refreshes on demand. Correct, but
   it throws away the entire point of NNUE — roughly two orders of magnitude of
   evaluation speed. The machinery it needs (`DescribeMove`, `ComputeDelta`,
   `ApplyDelta`) exists and is tested; what is missing is threading the captured
   piece from `ChessBoard::MakeMove` through to `Push`.
2. **SIMD kernels.** `SimdOps.h` is scalar only. AVX2, SSE4.1, and NEON paths
   belong behind the same interface, and must produce bit-identical results —
   NNUE inference has to be deterministic across machines, or two Lazy SMP
   workers searching the same position disagree and poison the shared
   transposition table.
3. **Training data.** No dataset and no way to generate one; `datagen.play_game`
   is unimplemented.
4. **A trained network.** Nothing to load yet.

### Suggested order

1. **Training data**, by borrowing labels from an existing engine. Everything
   else is unmeasurable without it.
2. **A first network**, even a weak one. This is the point where the engine
   plays chess again and where every piece of the pipeline gets validated
   against reality rather than against a fixture.
3. **Incremental updates**, once there is a network whose speed is worth
   measuring.
4. **SIMD kernels**, measured against the scalar path for exact agreement.
5. **Architecture growth** — king buckets, output buckets, a wider hidden layer
   — each one A/B tested against the last network rather than assumed.

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

The end-to-end check is `nnue_training.verify`, which requires the engine and
the trainer to produce identical integers for the same position and network:

```bash
cd NNUETraining && python3 scripts/make_random_network.py --output /tmp/random.nnue
```

```bash
cd NNUETraining && python3 -m nnue_training.verify --network /tmp/random.nnue --engine ../bin/Release/NeraChessUCI/NeraChessUCI
```

Tests that measure evaluation quality — `TestSearchChoices`, and the strength
half of the strategic benchmarks — skip themselves while no network is loaded
and print that they did. They come back automatically once one exists.
