# Training a network

How to produce a `.nnue` file and get the engine playing with it. Everything
below is the self-play pipeline: positions from NeraChess's own games, labelled
with NeraChess's own search.

That is not how the network that currently ships was labelled. Its positions
come from self-play, but its scores come from Stockfish, run as a separate
process; the tool that does that is not in this repository. See
[MODEL_CARD.md](MODEL_CARD.md) for the shipped network's lineage and for what
about it is not reproducible from this tree.

For the architecture and how inference works, see [NNUE.md](NNUE.md). This
document is the operational side.

---

## How it works

Training needs search scores, search needs an evaluation, and the evaluation is
the network being trained. Something has to break that circle.

The only knowledge injected is **piece values** — the same ones the move
ordering already uses for static exchange evaluation — and even those live in
the training data rather than in the engine:

**Generation 0.** `NeraChessSelfPlay --mode material` plays random legal moves
and labels each position with its material balance. Training on that yields a
network that evaluates material to within about 25 centipawns. It knows nothing
else, but that is enough to make the engine play non-random chess.

**Generation 1 and up.** `NeraChessSelfPlay` plays real games with the current
network, labelling every position with the search score and the eventual game
result. Training on those games produces the next network. Repeat.

Each generation learns things the last one had no concept of. Generation 1,
trained only on generation 0's games, already scores a centralized knight 68cp
above a rim knight and a pawn on the seventh 88cp above one on the second —
piece-square knowledge nobody wrote down anywhere.

---

## The pieces

| Component | Language | Job |
| --- | --- | --- |
| `NeraChessSelfPlay` | C++ | Plays games and writes labelled positions |
| `nnue_training.train` | Python | Trains a network and exports a `.nnue` |
| `nnue_training.verify` | Python | Proves the engine evaluates it exactly as the trainer does |
| `scripts/pipeline.py` | Python | Runs the whole loop and resumes where it left off |

Generation lives in C++ because the engine already has legal move generation,
game-over detection, and a search — roughly a hundred times the throughput of a
Python driver doing a UCI round-trip per move.

Samples are plain text, one position per line:

```text
<fen> | <score> | <result>
```

`score` is centipawns from the side-to-move point of view; `result` is the game
result from the same point of view (`1.0` win, `0.5` draw, `0.0` loss). About
68 bytes per position, so 2M positions is roughly 140 MB.

---

## On this Mac

### One-time setup

```sh
./scripts/Setup-macOS.sh gmake
```

```sh
make -C NeraChessEngine config=release && make -C NeraChessNNUE config=release && make -C NeraChessSearch config=release && make -C NeraChessUCI config=release && make -C NeraChessSelfPlay config=release
```

```sh
python3 -m venv NNUETraining/.venv && NNUETraining/.venv/bin/pip install -r NNUETraining/requirements.txt
```

### Run the loop

```sh
cd NNUETraining && .venv/bin/python scripts/pipeline.py --workdir runs/first --generations 5
```

That runs, for generation 0 and then each self-play generation: generate data,
train, and verify. Completed stages are reused, so an interrupted run picks up
where it stopped. Add `--force` to redo everything.

Resuming is safe because a stage's output only takes its real name once that
stage finishes: an interrupted generation leaves `genN.txt.partial`, which the
next run regenerates from scratch rather than trusting.

Each generation leaves `genN.txt` and `genN.nnue` in the work directory.

### Continuing from a network you already have

`--generations` is the *last generation number to produce*, not a count, and
`--start-generation` says where to begin. To carry on from the network the
engine ships with:

```sh
cd NNUETraining && .venv/bin/python scripts/pipeline.py --workdir runs/next --start-generation 43 --generations 50 --seed-network ../NeraChessApp/Resources/NNUE/nera.nnue
```

That copies the network in as `gen42.nnue` and runs generations 43 through 50
on top of it, skipping the material bootstrap entirely. Without
`--start-generation` the pipeline begins at generation 0 and regenerates
everything, which for an established run means hours of self-play to rediscover
what you already had.

If the work directory still contains `genN.nnue` from an earlier run, drop
`--seed-network` — the pipeline finds it by name.

The starting network is checked before anything expensive happens: a truncated
file or one built for a different architecture fails in a second rather than
after the first generation of games.

Nothing carries over between generations except the network itself. There is no
optimizer state to restore; each generation trains a fresh model on the games
its parent played.

### A first run worth trying

Start small enough to finish in an hour, so you see the whole loop before
committing to an overnight job:

```sh
cd NNUETraining && .venv/bin/python scripts/pipeline.py --workdir runs/quick --generations 3 --bootstrap-positions 500000 --positions 300000 --depth 4
```

---

## On a Linux server

### Build

Ubuntu or Debian needs a C++23 compiler and Python:

```sh
sudo apt-get update && sudo apt-get install -y build-essential g++-13 python3-venv python3-pip
```

The repository bundles Premake for Linux, so project generation needs nothing
extra:

```sh
bash ./scripts/Setup-Linux.sh gmake
```

```sh
make -C NeraChessEngine config=release -j"$(nproc)" && make -C NeraChessNNUE config=release -j"$(nproc)" && make -C NeraChessSearch config=release -j"$(nproc)" && make -C NeraChessUCI config=release -j"$(nproc)" && make -C NeraChessSelfPlay config=release -j"$(nproc)"
```

SDL is not needed — the desktop application is the only target that wants it,
and none of the above builds it.

**Build with AVX2 if the server has it.** The default x86 build uses SSE2, which
leaves the output layer scalar. Self-play is evaluation-bound, so this is the
single biggest speed lever:

```sh
grep -q avx2 /proc/cpuinfo && make -C NeraChessNNUE config=release -B -j"$(nproc)" CXXFLAGS="-mavx2" && make -C NeraChessSearch config=release -B -j"$(nproc)" CXXFLAGS="-mavx2" && make -C NeraChessSelfPlay config=release -B -j"$(nproc)" CXXFLAGS="-mavx2"
```

`NeraChessSelfPlay` prints which kernels it is using when it loads a network.

### Python

The CPU-only PyTorch wheel is a fraction of the size of the default one, and
the trainer sees no benefit from a GPU (see below):

```sh
python3 -m venv NNUETraining/.venv && NNUETraining/.venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cpu
```

### Run it so it survives your SSH session

A generation at depth 6 takes hours. Use `tmux`:

```sh
tmux new -s nnue
```

then inside that session:

```sh
cd NNUETraining && .venv/bin/python scripts/pipeline.py --workdir runs/server --generations 8 --positions 4000000 --depth 6 --threads "$(nproc)" 2>&1 | tee runs/server/log.txt
```

Detach with `Ctrl-B` then `D`; reattach later with `tmux attach -t nnue`.

Without tmux, `nohup ... &` works too, but you lose the ability to watch it.

### Generate on the server, train wherever

The two halves are independent — the sample files are the only interface. To
use the server purely as a game generator:

```sh
./bin/Release/NeraChessSelfPlay/NeraChessSelfPlay --network gen0.nnue --output gen1.txt --positions 4000000 --depth 6 --threads "$(nproc)"
```

```sh
scp server:~/NeraChess/gen1.txt .
```

Then train locally. The files compress well — `gzip` gets them to roughly a
quarter — and `--append` lets several machines contribute to one dataset.

---

## Choosing parameters

Measured on eight cores. Self-play dominates completely; training is minutes
against hours.

| Stage | Rate | For 2M positions |
| --- | --- | --- |
| Material labelling | ~850k positions/s | 2 seconds |
| Self-play, `--depth 4` | ~1600 positions/s | 21 minutes |
| Self-play, `--nodes 5000` | ~1235 positions/s | 27 minutes |
| Self-play, `--depth 6` | ~120 positions/s | 4.6 hours |
| Training, 20 epochs | ~1.5s per 200k per epoch | 5 minutes |

**Depth is the expensive knob** — two extra plies cost 13x. Keep it low for
early generations: while the network is weak, position variety is worth more
than label precision. Raise it once the network is strong enough that its deep
scores mean something. `--nodes` gives a more predictable budget per move than
`--depth`, since the cost of a depth does not stay constant as the network
improves.

**A GPU is not worth buying for this.** Training is bound by batch collation in
Python rather than by the gradient step; Apple MPS measured within 15% of CPU.
Spend the hardware on self-play cores instead — generation scales with them
almost linearly.

**Scale positions with generations.** Roughly 1M for generation 0, then 2-5M
per generation. More data helps more than more epochs: with a few hundred
thousand positions the network memorizes them.

Everything is `--seed` reproducible, so a run can be repeated exactly.

---

## Is the new generation actually better?

**Nothing in the pipeline checks this.** Training loss going down means the
network fits its data, not that it plays better — and a generation trained on
weak games can be worse than its parent. Test before promoting.

Both networks run in the same binary, so an A/B match is one engine against
itself with different `EvalFile` values. The repository has a runner for this:

```bash
cd NNUETraining && .venv/bin/pip install chess
```

```bash
cd NNUETraining && .venv/bin/python scripts/match.py --a runs/first/gen30.nnue --b runs/first/gen31.nnue --games 200
```

It plays colour-reversed pairs from shared random openings with a fixed node
budget per move, and reports the score with a confidence interval and an Elo
estimate. Treat small samples with suspicion: an 8-game run of exactly the
comparison above said one network was 191 Elo ahead; 200 games put it 31 Elo
behind, with the interval still spanning zero.

For a full tournament with SPRT, `cutechess-cli` does more:

```sh
cutechess-cli -engine cmd=./bin/Release/NeraChessUCI/NeraChessUCI name=gen2 initstr="setoption name EvalFile value $PWD/runs/first/gen2.nnue" -engine cmd=./bin/Release/NeraChessUCI/NeraChessUCI name=gen1 initstr="setoption name EvalFile value $PWD/runs/first/gen1.nnue" -each proto=uci tc=10+0.1 option.OwnBook=false -games 2 -rounds 300 -repeat -openings file=openings.pgn order=random -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 -concurrency 8
```

`-repeat` plays each opening twice with colours reversed, which removes most of
the variance from opening choice. Turn `OwnBook` off, or both sides play book
moves and you measure nothing.

Expect generations to improve for roughly five to ten rounds and then plateau,
at which point more data or a larger network is the next move rather than
another generation.

---

## Installing the network

Three ways, in order of convenience.

### 1. Bundled resource — both binaries find it

```sh
mkdir -p NeraChessApp/Resources/NNUE && cp runs/first/gen5.nnue NeraChessApp/Resources/NNUE/nera.nnue
```

Rebuild, and the desktop application copies it next to its executable. The
desktop bot loads it at startup and says so on the console; the UCI engine
finds it too, because it looks in `Resources/NNUE/` beside itself.

The pipeline can do this directly:

```sh
cd NNUETraining && .venv/bin/python scripts/pipeline.py --workdir runs/first --generations 5 --install ../NeraChessApp/Resources/NNUE/nera.nnue
```

### 2. Beside the executable

```sh
cp runs/first/gen5.nnue ./bin/Release/NeraChessUCI/nera.nnue
```

Both binaries look for `nera.nnue` next to themselves first. This is the layout
to use when shipping a build.

### 3. UCI option — for a GUI or a match

```text
setoption name EvalFile value /path/to/gen5.nnue
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

**`Self-play needs a network`** — self-play mode was run without `--network`, or
the path is wrong. Generation 0 comes from `--mode material`, which needs none.

**An interrupted run** — Ctrl-C, a killed job, a full disk — leaves a
`genN.txt.partial` file and no `genN.txt`. That is deliberate: the output is
only renamed into place once the run completes, so a resume regenerates the
stage instead of training on a fraction of it. Delete the `.partial` file, or
ignore it; the next run truncates it. Nothing needs cleaning up by hand.

If you have a truncated `genN.txt` from before this behaviour existed, the
trainer will read it, warn about the torn final line, and train on whatever is
there. That is usually not what you want when the file is a small fraction of
the requested size — delete it and regenerate.

**Nearly every game is a draw** — adjudication is off or too lenient. Check
`--win-score` and `--win-plies`. A healthy generation-0 run is around 70-75%
decisive; if it is far below that, the network is too weak to convert and a
lower `--win-score` will help.

**`network file was trained for a different architecture`** — the `.nnue`
predates a change to `NetworkArchitecture.h`. Networks are not portable across
architecture changes and cannot be converted; retrain.

**`verify` reports disagreements** — the trainer and the engine no longer
implement the same network. This is a bug, not a tuning problem, and no amount
of training will fix it. Check that `architecture.py` still matches
`NetworkArchitecture.h`.

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

**Self-play is slower than the table above** — check the kernel line the tool
prints. `kernels sse2` on x86 means the output layer is scalar; rebuild with
`-mavx2`.
