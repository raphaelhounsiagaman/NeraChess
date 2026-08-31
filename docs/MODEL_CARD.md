# Model card — `nera.nnue`

The single record of what the shipped network is and where it came from. Every
other document should link here rather than restating training lineage; that
duplication is what let the repository carry two contradictory accounts at once.

## Identity

| Field | Value |
| --- | --- |
| Path | [`NeraChessApp/Resources/NNUE/nera.nnue`](../NeraChessApp/Resources/NNUE/nera.nnue) |
| SHA-256 | `58202d2c2a3626b65a8111d3c57661c9d2e73b62b810d887929781c11e5d9ec8` |
| Size | 6,294,578 bytes |
| Generation | binpack-kingbuckets-005, epoch 24 |
| Shipped in | candidate, not yet promoted |
| Architecture | `(768x8 -> 512)x2 -> 1`, 8 input buckets, 1 output bucket |
| Feature set | 3 — horizontally canonicalized, then bucketed on the own king |
| Architecture hash | `0x30346d9d` |
| Quantization | QA 255, QB 64, eval scale 400 |
| Activation | Squared clipped ReLU |
| Parent | binpack-mirrored-004 epoch 17, tiled into feature set 3 (see below) |

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
were produced after that change, and generation 61 is where the binpack runs
below started from.

### How this one was made

Four runs, all warm-started, all at learning rate 1e-4 with 1000 warmup steps,
loss scaling 626.1, and source scores multiplied by 0.480769.

`binpack-warm-001` started from generation 61 and ran 8 epochs of 100M
positions (800M total). `binpack-warm-002` continued from its epoch 8 and ran
24 epochs of 250M positions (6B total). `binpack-long-003` continued from that
and ran 20 epochs of 500M positions (10B total) before being stopped short of
its 90-epoch schedule, because validation had flattened. Together that is
16.8B positions, a little over one full pass through the corpus.

`binpack-mirrored-004` is the first trained under
feature set 2. It ran 22 epochs of 500M positions (11B, one more full pass)
with lambda continuing run 003's anneal from 0.797, and was stopped at epoch 22
by the rule described below. Its best epoch is 17, at 8.5B positions into the
run.

`binpack-kingbuckets-005` is this network, and it is the first trained under
feature set 3. It ran 28 epochs of 500M positions (14B, a little over one more
full pass) with lambda continuing run 004's anneal from 0.797, at the same
learning rate 1e-4 as every run before it, and was stopped at epoch 28 by the
rule described below. Its best epoch is 24, at 12B positions into the run.

Every hyperparameter matches run 004's, so the architecture is the only
variable between them and their validation curves compare directly. Run 005 is
below run 004 at *every* epoch by roughly 3%: its epoch 1 (0.002279) already
beat run 004's best-ever (0.002262), and its own best of 0.002171 is 4.0%
below that. That is the capacity hypothesis behaving as predicted -- eight
weight matrices where there was one -- and it is still only a loss number.

**Run 005's warm start crossed a feature-set boundary, exactly.** King buckets
change a dimension, so run 004's network could not be loaded directly.
`NNUETraining/scripts/expand_king_buckets.py` tiled it: every bucket got the
same weight matrix. Unlike the port below, that is an exact conversion -- a
feature index is `bucket * 768 + within`, so identical blocks make the bucket
term select the same row whichever bucket it names. The seed evaluated every
position identically to run 004's epoch 17, which was verified before training
started by comparing both engines' `eval` over a 36-position suite spanning all
eight buckets and their depth-12 node counts. So run 005 began *at* run 004's
strength, and its whole job was pulling the eight copies apart.

**Run 004's warm start crossed a feature-set boundary too, inexactly.**
Horizontal canonicalization
renumbered every feature without changing a dimension, so run 003's network
could not be loaded directly. `NNUETraining/scripts/port_feature_set.py`
re-headered it: the payload is byte-identical and only the architecture hash
differs. That reinterprets the weights rather than converting them, and no
conversion exists -- for a perspective whose king is on files e-h the numbering
did not change, and for one on files a-d the new network reads the old
network's weights for the reflected square, which horizontal symmetry makes
approximately the same answer. The seed therefore started near run 003's
strength rather than at run 003's strength, and the first epochs are the
network reconciling the difference.

Validation is 50,000 positions reservoir-sampled from 64 chunks reserved by
seed and scattered across the whole file. Run 004 reuses `--seed 1`, so it
holds out the *same* chunks as run 003 and the two curves compare directly:
0.003550 at the start of run 001, 0.002829 at its end, 0.002458 at epoch 24 of
run 002, 0.002362 at epoch 20 of run 003, **0.002262 at epoch 17 of run 004**.

Run 003 is where the unmirrored network stopped paying. Its first sixteen
epochs moved the running minimum from 0.002458 to 0.002365; its last four moved
it to 0.002362, about 1e-6 per epoch against epoch-to-epoch swings of 15e-6. At
that ratio a new "best" is what tracking the minimum of a noisy series produces
on its own, so the run was stopped rather than left to spend another 68 hours
on it.

Run 004 recovered that endpoint in a single epoch -- 0.002353 after 500M
positions, already below run 003's final 0.002362 -- and went on to 0.002262,
4.2% below it, on identical data and an identical parameter count. Sharing one
representation between a pattern and its horizontal mirror is the only thing
that changed, so that is what the 4.2% is attributable to.

Run 004 was stopped at epoch 22 on the same kind of evidence that stopped run
003, applied as a written rule rather than by eye: a floor of one full pass
over the corpus, then stop once the running minimum has improved by less than
0.15% over five epochs with at least three of them setting no new best. Epochs
18-22 set no new best at all and moved the minimum by 0.000%. The plausible
limit remains capacity -- 394,753 parameters -- rather than data.

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

**This network has not been promoted.** Its strength test against `main` is
running; until it returns a verdict, the only evidence for king buckets is a
validation curve, and this project's own record is that validation loss and
Elo are different quantities. Its parent moved validation 4.2% and the games
+7.3 Elo with an interval spanning zero.

Its parent, `binpack-mirrored-004` epoch 17, was measured against the commit
`main` was at before horizontal mirroring, over 1,000 games with the search
identical on both sides:

| Games | Result | Score | Elo | 95% CI |
| --- | --- | --- | --- | --- |
| 1000 | 307-286-407 | 0.5105 | +7.3 | [-7.5, +22.2] |

That interval contains zero, so it too was inconclusive; it shipped as a
candidate because the shipped network had to change anyway, its feature set
having ceased to exist. What that match established is the absence of a
regression, not a gain. See
[NNUE_PROGRESS.md](NNUE_PROGRESS.md#2026-08-27--horizontal-mirroring-measured)
for the conditions, which differ from every earlier row.

Generation 61, the last network promoted on its own evidence, cleared the bar
against generation 60 with the search identical on both sides:

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
- **One output head.** Output buckets by material count remain unbuilt; the
  dimension exists with a count of 1.
- **Strength is unmeasured against any external reference** since the NNUE
  migration.
- **Its own gain is unmeasured.** The strength test against `main` has not
  returned. Everything currently known about king buckets is a 4.0% validation
  improvement over the previous best, and the immediately preceding generation
  is a standing reminder that a validation gain of that size bought an Elo
  interval spanning zero.
- **Rarely-visited buckets are barely trained.** A king reaches the far ranks
  in a small fraction of positions, so buckets 6 and 7 saw far less gradient
  than 0 and 1 and remain close to the tiled seed. The capacity is allocated
  by the map, not by the data; re-tuning `KingBucketTable` is the lever, and
  changing it needs a `FeatureSetVersion` bump.
- **The run was slower than its schedule for reasons outside it.** Epoch times
  roughly doubled over the run while CPU utilisation stayed constant. The host
  was measured delivering 30-40% less throughput on identical fixed work with
  the trainer paused, at full nominal clock and ~0 guest-visible steal. Nothing
  in the training path accounts for it, and it affects wall-clock cost only.

## License

The network is covered by the repository's [MIT license](../LICENSE). It is a
parameter file produced by this project's training pipeline; it embeds no
third-party code.
