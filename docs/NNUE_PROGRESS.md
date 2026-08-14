# NNUE strength log

The single running record of how the network compares to a reference engine.
One row per match. Nothing is promoted on validation loss; only a match result
with a confidence interval that excludes zero counts as a gain.

Every row records both commits, because a strength number means nothing without
knowing which search produced it. Until commit `81bbd70` the `NNUE` branch was
missing `main`'s selectivity work (late-move-count pruning, internal iterative
reduction, the improving stack), so any comparison made before that date was
measuring search and evaluation together.

## Conditions

Unless a row says otherwise:

- `cutechess-cli`, 10+0.1, one thread per engine, 64 MB hash, `OwnBook=false`.
- Openings from `/opt/chess-books/8moves_v3.pgn`, 8 plies, random order,
  `-repeat` so each opening is played once with each colour.
- Both engines pinned to cores 2-3 with `taskset`. Cores 0-1 belong to the
  Lichess bot. Generation was not running: a match against busy cores measures
  scheduling noise, not strength.
- Elo and the interval are cutechess's own, which is a 95% interval.

## Results

| Date | Net | Commit (net) | Opponent | Commit (opp) | Games | Score | Elo | 95% CI | LOS |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-08-14 | gen42 | `81bbd70` | main (classical) | `0afdd77` | 500 | 203-165-132 (0.538) | +26.5 | ±26.2 | 97.7% |
| 2026-08-15 | gen42 | `81bbd70` | main (classical) | `0afdd77` | 500 | 212-176-112 (0.536) | +25.1 | ±26.9 | 96.7% |
| 2026-08-15 | gen42 | `81bbd70` | main (classical) | `0afdd77` | **1000** | **415-341-244 (0.537)** | **+25.8** | **±18.8** | **99.65%** |

The third row is the first two combined, not a third match: two independent
500-game runs under identical conditions with different opening seeds
(`-srand 20260814` and `-srand 20260815`). They agree closely (+26.5 and
+25.1), which is the main reason to trust the pooled figure.

## Notes

### 2026-08-14 — first measurement with the search equalised

This is the first comparison in which the two branches differ *only* in
evaluation. The difference between `0afdd77` and `81bbd70` outside the NNUE
module is confined to evaluation plumbing: the accumulator stack, the
`Evaluate` to `EvaluateNode` substitutions, and the make/unmake pairing that
keeps the accumulator aligned with the board. `MoveOrdering.cpp` is identical,
every selectivity constant and formula is identical, and `NeraChessEngine`
differs by one added `#include`. So this number is the evaluation gap.

The result is the opposite of what the training plan assumed. The plan is
written around bootstrapping a weaker NNUE from `main`'s classical evaluation;
in fact the network already beats it, by a small but significant margin. The
interval excludes zero, though only just.

The gen43-gen46 gate failures recorded in the training loop's history are
NNUE-versus-NNUE regressions. They say nothing about NNUE versus classical, and
they were produced by the pre-merge search.

### 2026-08-15 — the parity gate is met

A second 500 games under identical conditions returned +25.1, close enough to
the first run's +26.5 that the two pool cleanly. Over the combined 1000 games
the network is +25.8 Elo with a 95% interval of [+7.1, +44.6].

That satisfies the parity gate as written: a network at or above `main` over
1000 games at 10+0.1 with an interval excluding zero. The gate was expected to
take a long bootstrapping programme to reach. It was in fact already met once
the search was equalised — the branch had been carrying an evaluation lead and
a search deficit at the same time, and only the second one was visible.

The gate is a trigger for a decision, not for an automatic merge, and the
decision is the maintainer's. Nothing here has been merged or promoted.

Time forfeits were 4 in 1000 games (0.4%), split between both engines, so time
management did not distort the result.

Raw match output and games: `/srv/nera-nnue/matches/phase0-baseline/` and
`/srv/nera-nnue/matches/phase0-confirm/`.
