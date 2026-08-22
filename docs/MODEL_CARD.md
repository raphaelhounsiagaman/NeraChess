# Model card — `nera.nnue`

The single record of what the shipped network is and where it came from. Every
other document should link here rather than restating training lineage; that
duplication is what let the repository carry two contradictory accounts at once.

## Identity

| Field | Value |
| --- | --- |
| Path | [`NeraChessApp/Resources/NNUE/nera.nnue`](../NeraChessApp/Resources/NNUE/nera.nnue) |
| SHA-256 | `5a5853d6614e886ed9c39f5f99a4b11a9a537a2d190506b4db959931f4d31679` |
| Size | 789,554 bytes |
| Generation | 61 |
| Shipped in | [`853238a`](https://github.com/raphaelhounsiagaman/NeraChess/commit/853238a2e46f8652639a5be25cda8054f19479c4), 2026-08-21 |
| Architecture | `(768 -> 512)x2 -> 1`, 1 input bucket, 1 output bucket |
| Architecture hash | `0x53e8d097` |
| Quantization | QA 255, QB 64, eval scale 400 |
| Activation | Squared clipped ReLU |
| Parent | generation 60, shipped in [`ef50fdb`](https://github.com/raphaelhounsiagaman/NeraChess/commit/ef50fdb) |

Verify the file you have is the file described here:

```sh
sha256sum NeraChessApp/Resources/NNUE/nera.nnue
```

## Training data

**Positions** come from NeraChess's own play. Nothing else generates them.
`NeraChessSelfPlay` plays the games; see [TRAINING.md](TRAINING.md).

**Labels** come from Stockfish, run as a separate process and asked for a score
for a position. This changed at [`2adf122`](https://github.com/raphaelhounsiagaman/NeraChess/commit/2adf122)
(2026-08-17): before it, positions were labelled with NeraChess's own search,
and that had a ceiling — a network trained on its own search chases itself, and
generation 42 is roughly where it stopped improving. Generations 58, 60, and 61
were produced after that change; generation 61 is what ships.

No Stockfish code is copied, derived from, linked against, or vendored. The
repository contains no Stockfish source and no Stockfish binary. The
relationship is that of a separate program asked for a number.

### What is not reproducible from this repository

Stated plainly, because the gap matters more than a claim of completeness:

- **The labelling tool is not in the tree.** `docs/NNUE.md` used to point at
  `experiments/sflabel.py`; no such file has ever been committed. The
  checked-in pipeline (`NNUETraining/scripts/pipeline.py`) performs pure
  self-play labelling and is the generation-42-and-earlier method.
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

- **Score scale is uncalibrated.** The network calls a queen roughly +2800
  rather than +900. Move ordering only cares about ordering, but the search's
  pruning margins are denominated in centipawns, so they are effectively
  tighter than their constants suggest, and scores shown by a GUI are
  misleading. See [NNUE.md](NNUE.md).
- **No king buckets and one output head.** The smallest architecture worth
  training, chosen for simplicity over strength.
- **Strength is unmeasured against any external reference** since the NNUE
  migration.

## License

The network is covered by the repository's [MIT license](../LICENSE). It is a
parameter file produced by this project's training pipeline; it embeds no
third-party code.
