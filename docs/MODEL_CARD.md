# Model card — `nera.nnue`

The single record of what the shipped network is and where it came from. Every
other document should link here rather than restating training lineage; that
duplication is what let the repository carry two contradictory accounts at once.

## Identity

| Field | Value |
| --- | --- |
| Path | [`NeraChessApp/Resources/NNUE/nera.nnue`](../NeraChessApp/Resources/NNUE/nera.nnue) |
| SHA-256 | `1ec594a7fe5d4df0a431adaa5f616ce418570e03b4aef8454919bba0ce6c94f9` |
| Size | 789,554 bytes |
| Generation | binpack-long-003, epoch 20 |
| Shipped in | candidate, not yet promoted |
| Architecture | `(768 -> 512)x2 -> 1`, 1 input bucket, 1 output bucket |
| Architecture hash | `0x53e8d097` |
| Quantization | QA 255, QB 64, eval scale 400 |
| Activation | Squared clipped ReLU |
| Parent | binpack-warm-002 epoch 24, shipped in [#15](https://github.com/raphaelhounsiagaman/NeraChess/pull/15) |

Verify the file you have is the file described here:

```sh
sha256sum NeraChessApp/Resources/NNUE/nera.nnue
```

## Training data

**Positions and labels both** come from a published Stockfish training corpus:
`nodes5000pv2_UHO.binpack` from
[official-stockfish/master-binpacks](https://huggingface.co/datasets/official-stockfish/master-binpacks),
repo commit `1e095a75`, 40,292,454,358 bytes, SHA-256
`7a80e6d233d4df954e162e0d992b768bf1799154289f08299735ddd1ba2bdc34`. That is a
change of kind from every earlier generation, which used NeraChess's own play
for positions.

The earlier lineage still explains the parent. Labels moved to Stockfish at
[`2adf122`](https://github.com/raphaelhounsiagaman/NeraChess/commit/2adf122)
(2026-08-17); before it, positions were labelled with NeraChess's own search,
and that had a ceiling — a network trained on its own search chases itself, and
generation 42 is roughly where it stopped improving. Generations 58, 60, and 61
were produced after that change, and generation 61 is this network's parent.

### How this one was made

Two runs, both warm-started, both at learning rate 1e-4 with 1000 warmup steps,
loss scaling 626.1, and source scores multiplied by 0.480769.

`binpack-warm-001` started from generation 61 and ran 8 epochs of 100M
positions (800M total). `binpack-warm-002` continued from its epoch 8 and ran
24 epochs of 250M positions (6B total). `binpack-long-003` continued from that
and ran 20 epochs of 500M positions (10B total) before being stopped short of
its 90-epoch schedule, because validation had flattened. Together that is
16.8B positions, a little over one full pass through the corpus.

Validation is 50,000 positions reservoir-sampled from 64 chunks reserved by
seed and scattered across the whole file, held fixed across both runs so the
numbers compare: 0.003550 at the start of run 001, 0.002829 at its end,
0.002458 at epoch 24 of run 002, 0.002362 at epoch 20 of run 003.

Run 003 is where it stopped paying. Its first sixteen epochs moved the running
minimum from 0.002458 to 0.002365; its last four moved it to 0.002362, about
1e-6 per epoch against epoch-to-epoch swings of 15e-6. At that ratio a new
"best" is what tracking the minimum of a noisy series produces on its own, so
the run was stopped rather than left to spend another 68 hours on it. The
plausible limit is capacity -- 394,753 parameters -- rather than data.

One avoidable cost is recorded because it is easy to repeat: run 002 was
started with the default `--lambda-start 1.0` rather than continuing run 001's
schedule from 0.889. Since validation is measured at a fixed lambda of 0.7,
that made the metric jump backwards and it took eleven epochs to recover.

Unlike every previous generation, this one **is** reproducible from tooling that
is committed: the run's exact configuration, including the reserved validation
chunk ids, is written to `config.json` beside the network by the trainer.

Neither step is performed by anything in this repository. The in-tree game
generator that produced positions for the earliest generations,
`NeraChessSelfPlay`, was removed once external tooling took over both halves;
it is recoverable from history at
[`b4c2c80`](https://github.com/raphaelhounsiagaman/NeraChess/commit/b4c2c80)
and earlier, but it is not the method behind the shipped network. What this
repository does with a sample file, once one exists, is
[TRAINING.md](TRAINING.md).

No Stockfish code is copied, derived from, linked against, or vendored. The
repository contains no Stockfish source and no Stockfish binary. The
relationship is that of a separate program asked for a number.

### What is not reproducible from this repository

Stated plainly, because the gap matters more than a claim of completeness:

- **Neither the position generator nor the labelling tool is in the tree.**
  `docs/NNUE.md` used to point at `experiments/sflabel.py`; no such file has
  ever been committed. What this repository now contains is the training half
  only: it reads a sample file and produces a network.
- **The Stockfish version, depth or node budget, and exact invocation are not
  recorded.**
- **The training corpus is not published**, and no checksum of it was kept, so
  the exact input to generation 61 cannot be reconstructed.
- **Training hyperparameters for this generation were not captured** beyond the
  pipeline defaults.

Anyone reproducing this network should expect to arrive at a different one.
Filling these in requires records only the author holds; until they are filled
in, the network is verifiable by checksum but not reproducible.

## Promotion evidence

Generation 61 was promoted on a match against generation 60 with the search
identical on both sides:

| Games | Result | Score | Elo | 95% CI | LOS |
| --- | --- | --- | --- | --- | --- |
| 700 | 218-131-351 | 0.5621 | +43.4 | [+25.3, +61.7] | 100.0% |

The bar for shipping is an interval that excludes zero; scoring above 50% is not
enough, because an interval containing zero means the match could not
distinguish the two networks. The full log is
[NNUE_PROGRESS.md](NNUE_PROGRESS.md).

Note what this measures: each generation is compared to the one before it, not
to a fixed reference. The per-generation Elo figures do not add up to a total,
and no current measurement places generation 61 on the scale used in
[ENGINE_STRENGTH.md](ENGINE_STRENGTH.md).

## Known limitations

- **Score scale, corrected.** Earlier revisions of this document said the
  network "calls a queen roughly +2800 rather than +900". That figure was never
  measured and does not hold: regressed against Stockfish evaluations over 1500
  corpus positions, generation 61 reports 0.31 engine centipawns per unit of
  Stockfish's internal score, which puts a queen near +580 — low, not high.
  This network was trained with labels deliberately scaled to real centipawns
  and measures 0.5535, putting a queen at +1036. Note that it drifted about 10%
  above the 0.4808 it was trained toward, and drifting further with each run: as lambda anneals, game results enter
  the objective and pull evaluations larger, which is the grain of truth in the
  claim this entry replaces. The search's pruning margins are denominated in
  centipawns, so this changes what they mean; that is a reason to retune them,
  and a reason not to read a strength result here as isolating the data change.
- **Trained for feature-set version 1, which no longer exists.** This network
  predates the horizontal canonicalization described in
  [NNUE.md](NNUE.md#horizontal-canonicalization). Its weights were learned
  against the old numbering, so the engine now refuses it with
  `ArchitectureMismatch` rather than reading them under a mapping they were
  never trained for. A replacement has to be trained; there is no conversion.
- **No king buckets and one output head.** The smallest architecture worth
  training, chosen for simplicity over strength.
- **Strength is unmeasured against any external reference** since the NNUE
  migration.

## License

The network is covered by the repository's [MIT license](../LICENSE). It is a
parameter file produced by this project's training pipeline; it embeds no
third-party code.
