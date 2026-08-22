# Training a network

How to turn a file of labelled positions into a `.nnue` file and get the engine
playing with it.

**This repository does not generate training data.** Positions and their labels
are produced by tooling that is not in this tree — see
[MODEL_CARD.md](MODEL_CARD.md) for the shipped network's lineage and for what
about it is not reproducible from here. What follows starts from a sample file
you already have.

For the architecture and how inference works, see [NNUE.md](NNUE.md). This
document is the operational side.

---

## The pieces

| Component | Language | Job |
| --- | --- | --- |
| `nnue_training.train` | Python | Trains a network and exports a `.nnue` |
| `nnue_training.verify` | Python | Proves the engine evaluates it exactly as the trainer does |
| `scripts/match.py` | Python | Plays two networks against each other to see which is better |

Samples are plain text, one position per line:

```text
<fen> | <score> | <result>
```

`score` is centipawns from the side-to-move point of view; `result` is the game
result from the same point of view (`1.0` win, `0.5` draw, `0.0` loss). About
68 bytes per position, so 2M positions is roughly 140 MB. Blank lines and lines
starting with `#` are ignored, so a file may carry a header describing how it
was produced — recording that is worth the one line.

The reader tolerates a torn final line, which is what an interrupted writer
leaves behind. A malformed line anywhere else is fatal.

---

## Setup

### Build the engine

Only the UCI binary is needed, and only for `verify` and for matches:

```sh
bash ./scripts/Setup-Linux.sh gmake
```

```sh
make -C NeraChessEngine config=release -j"$(nproc)" && make -C NeraChessNNUE config=release -j"$(nproc)" && make -C NeraChessSearch config=release -j"$(nproc)" && make -C NeraChessUCI config=release -j"$(nproc)"
```

SDL is not needed — the desktop application is the only target that wants it,
and none of the above builds it. On macOS use `./scripts/Setup-macOS.sh gmake`.

Ubuntu or Debian needs a C++23 compiler and Python first:

```sh
sudo apt-get update && sudo apt-get install -y build-essential g++-13 python3-venv python3-pip
```

### Python

The CPU-only PyTorch wheel is a fraction of the size of the default one, and
the trainer sees no benefit from a GPU (see below):

```sh
python3 -m venv NNUETraining/.venv && NNUETraining/.venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cpu
```

---

## Training

```sh
cd NNUETraining && .venv/bin/python -m nnue_training.train --data samples.txt --output net.nnue --epochs 20
```

Then check that the engine agrees with the trainer about what the network
computes:

```sh
cd NNUETraining && .venv/bin/python -m nnue_training.verify --network net.nnue --engine ../bin/Release/NeraChessUCI/NeraChessUCI
```

A disagreement here is a bug, not a tuning problem: the trainer and the engine
are not implementing the same network, and no amount of training fixes that.

The output only takes its real name once training finishes — an interrupted run
leaves `net.nnue.partial` and no `net.nnue`, so a half-written network is never
mistaken for a finished one.

### Continuing from a network you already have

`--init-from` starts from existing weights instead of a random initialization:

```sh
cd NNUETraining && .venv/bin/python -m nnue_training.train --data samples.txt --output next.nnue --init-from ../NeraChessApp/Resources/NNUE/nera.nnue --epochs 20
```

Nothing else carries over. There is no optimizer state to restore.

### Choosing parameters

**A GPU is not worth buying for this.** Training is bound by batch collation in
Python rather than by the gradient step; Apple MPS measured within 15% of CPU.
Training runs in minutes on eight cores — roughly 1.5s per 200k positions per
epoch.

**More data helps more than more epochs.** With a few hundred thousand
positions the network memorizes them.

**`--memmap`** reads the sample pack from disk instead of loading it into
memory, which is what makes a dataset larger than RAM trainable.

Everything is `--seed` reproducible, so a run can be repeated exactly.

---

## Is the new network actually better?

**Nothing in training checks this.** Training loss going down means the network
fits its data, not that it plays better. Test before promoting.

Both networks run in the same binary, so an A/B match is one engine against
itself with different `EvalFile` values. The repository has a runner for this:

```bash
cd NNUETraining && .venv/bin/pip install chess
```

```bash
cd NNUETraining && .venv/bin/python scripts/match.py --a old.nnue --b new.nnue --games 200
```

It plays colour-reversed pairs from shared random openings with a fixed node
budget per move, and reports the score with a confidence interval and an Elo
estimate. Treat small samples with suspicion: an 8-game run of one comparison
said a network was 191 Elo ahead; 200 games put it 31 Elo behind, with the
interval still spanning zero.

For a full tournament with SPRT, `cutechess-cli` does more:

```sh
cutechess-cli -engine cmd=./bin/Release/NeraChessUCI/NeraChessUCI name=new initstr="setoption name EvalFile value $PWD/new.nnue" -engine cmd=./bin/Release/NeraChessUCI/NeraChessUCI name=old initstr="setoption name EvalFile value $PWD/old.nnue" -each proto=uci tc=10+0.1 option.OwnBook=false -games 2 -rounds 300 -repeat -openings file=openings.pgn order=random -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 -concurrency 8
```

`-repeat` plays each opening twice with colours reversed, which removes most of
the variance from opening choice. Turn `OwnBook` off, or both sides play book
moves and you measure nothing.

The bar for shipping a network is a confidence interval that excludes zero;
scoring above 50% is not enough, because an interval containing zero means the
match could not distinguish the two networks.

---

## Installing the network

Three ways, in order of convenience.

### 1. Bundled resource — both binaries find it

```sh
mkdir -p NeraChessApp/Resources/NNUE && cp net.nnue NeraChessApp/Resources/NNUE/nera.nnue
```

Rebuild, and the desktop application copies it next to its executable. The
desktop bot loads it at startup and says so on the console; the UCI engine
finds it too, because it looks in `Resources/NNUE/` beside itself.

### 2. Beside the executable

```sh
cp net.nnue ./bin/Release/NeraChessUCI/nera.nnue
```

Both binaries look for `nera.nnue` next to themselves first. This is the layout
to use when shipping a build.

### 3. UCI option — for a GUI or a match

```text
setoption name EvalFile value /path/to/net.nnue
```

Set it in your chess GUI's engine options, or via `initstr` as in the match
command above. This is the only way to run two networks side by side.

### Confirming it took

```sh
printf 'uci\nquit\n' | ./bin/Release/NeraChessUCI/NeraChessUCI | grep "info string network"
```

A loaded network reports its shape and kernels:

```text
info string network nera.nnue 768x1 -> 512x2 -> 1x1 (qa 255, qb 64, scale 400), kernels neon
```

If instead you see `no network loaded`, the engine is evaluating every position
as `0` and playing on search alone. The `eval` command reports the same thing
for a specific position.

**Networks are gitignored.** `*.nnue` is excluded, so a trained network never
lands in the repository by accident. Commit one deliberately with `git add -f`
if you want it shipped.

---

## Troubleshooting

**`network file was trained for a different architecture`** — the `.nnue`
predates a change to `NetworkArchitecture.h`. Networks are not portable across
architecture changes and cannot be converted; retrain.

**`verify` reports disagreements** — the trainer and the engine no longer
implement the same network. This is a bug, not a tuning problem, and no amount
of training will fix it. Check that `architecture.py` still matches
`NetworkArchitecture.h`.

**A warning about a torn final line** — the sample file was truncated, usually
by an interrupted writer. The trainer reads what is there and carries on. That
is usually not what you want when the file is a small fraction of the size you
expected — regenerate it.

**Training loss rises across epochs** — expected, and not a sign of trouble.
With `--lambda-end` below `--lambda-start` the objective anneals from "predict
the search score" toward "predict the game result", and game results are
irreducibly noisy per position: a position scored +50 can come from a lost
game, and nothing can predict that from the position alone. So the achievable
loss climbs as lambda falls, and `train` climbs with it. Losses measured at
different lambdas are not comparable.

Watch the `val` column instead. It is measured on held-out positions at a fixed
lambda every epoch, so it means the same thing throughout and is the number
that tells you whether the network is improving. A typical healthy run looks
like this — training loss rising, validation loss falling:

```text
epoch  3/10 train 0.000632 (lambda 0.933)  val 0.004650 *best
epoch  6/10 train 0.000963 (lambda 0.833)  val 0.003290 *best
epoch 10/10 train 0.001635 (lambda 0.700)  val 0.001680 *best
```

If `val` stops improving while `train` keeps falling, that is real overfitting:
use more positions rather than more epochs. If `val` rises from the start, lower
the learning rate. `--validation-fraction 0` turns the holdout off.
