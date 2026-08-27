# NNUE strength log

The single running record of how the network compares to a reference engine.
One row per match. Nothing is promoted on validation loss; only a match result
with a confidence interval that excludes zero counts as a gain.

A row should record both commits, because a strength number means nothing
without knowing which search produced it. The gen42 rows do. The rows from
gen48 onward carry `verified` instead: those matches were run by the
generation pipeline against the previous generation with the search unchanged
on both sides, and the engine commit was not captured at the time. They are
comparisons between consecutive networks, not against a fixed reference, so
their Elo figures do not compose into a total and cannot be reproduced exactly.
Rows added from here on should carry real hashes.

Until commit `23645a8` the `NNUE` branch was
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
| 2026-08-14 | gen42 | `23645a8` | main (classical) | `96c0d15` | 500 | 203-165-132 (0.538) | +26.5 | ±26.2 | 97.7% |
| 2026-08-15 | gen42 | `23645a8` | main (classical) | `96c0d15` | 500 | 212-176-112 (0.536) | +25.1 | ±26.9 | 96.7% |
| 2026-08-15 | gen42 | `23645a8` | main (classical) | `96c0d15` | **1000** | **415-341-244 (0.537)** | **+25.8** | **±18.8** | **99.65%** |
| 2026-08-15 | gen48 | `verified` | previous verified net | `verified` | 500 | 267-95-138 (0.672) | +124.6 | [+98.4, +152.3] | 100.0% |
| 2026-08-15 | gen52 | `verified` | previous verified net | `verified` | 400 | 195-84-121 (0.6387) | +99.0 | [+70.5, +128.8] | 100.0% |
| 2026-08-16 | gen55 | `verified` | previous verified net | `verified` | 400 | 165-97-138 (0.585) | +59.6 | [+32.2, +87.8] | 100.0% |
| 2026-08-16 | gen56 | `verified` | previous verified net | `verified` | 400 | 149-117-134 (0.54) | +27.9 | [+0.2, +55.9] | 97.57% |
| 2026-08-17 | gen58 | `verified` | previous verified net | `verified` | 400 | 185-116-99 (0.5863) | +60.5 | [+31.1, +90.9] | 100.0% |
| 2026-08-18 | gen60 | `verified` | previous verified net | `verified` | 400 | 168-82-150 (0.6075) | +75.9 | [+49.1, +103.6] | 100.0% |
| 2026-08-21 | gen61 | `verified` | previous verified net | `verified` | 700 | 218-131-351 (0.5621) | +43.4 | [+25.3, +61.7] | 100.0% |
| 2026-08-27 | binpack-mirrored-004 ep17 | `859ec03` | pre-mirroring main | `2daf702` | 1000 | 307-286-407 (0.5105) | +7.3 | [-7.5, +22.2] | n/a |

<!-- New rows go directly above this line, inside the table: a blank line
     between rows ends the table and everything below it stops rendering as
     one. There is no script that inserts them; add them by hand. -->

The third row is the first two combined, not a third match: two independent
500-game runs under identical conditions with different opening seeds
(`-srand 20260814` and `-srand 20260815`). They agree closely (+26.5 and
+25.1), which is the main reason to trust the pooled figure.

## Notes

### 2026-08-14 — first measurement with the search equalised

This is the first comparison in which the two branches differ *only* in
evaluation. The difference between `96c0d15` and `23645a8` outside the NNUE
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

### 2026-08-27 — horizontal mirroring, measured

The first row not produced under the conditions above. It comes from the
`Strength test` GitHub Actions workflow rather than from a local `cutechess-cli`
run: `fastchess`, 10+0.1, 128 MB hash, one thread per engine, openings from
`UHO_4060_v4.epd`, 1,000 games split across four shards with fixed seeds. The
harness reports an approximate paired 95% interval and no LOS, hence the `n/a`.
Numbers from this workflow and numbers from the local runner are not
interchangeable.

The candidate is the branch that introduced horizontal feature canonicalization
together with the first network trained under it; the baseline is the commit
`main` was at when that branch was cut. Search is byte-identical on both sides,
so this is an evaluation-versus-evaluation comparison — but it moves the feature
set and the network together and cannot separate them.

**+7.3 Elo, [-7.5, +22.2].** The interval contains zero, so this does not clear
the bar for promotion and is not being read as a gain. What it does exclude is a
regression worse than about 8 Elo, which is the question that mattered here:
the feature set changed underneath the network, and the network was retrained on
the same corpus that had already stopped paying, so the plausible bad outcome
was losing ground rather than gaining it. That did not happen.

Validation moved further than the games did — 0.002362 to 0.002262, 4.2% on the
same held-out chunks — which is the usual reminder that validation loss and Elo
are different quantities. The training-side reasoning is in
[MODEL_CARD.md](MODEL_CARD.md#how-this-one-was-made).

Worth separating from the Elo figure: mirroring halves the distinct positions
the network has to learn from, since a position and its reflection are now one
input. The corpus was already exhausted at this capacity, so that saving has
nothing left to buy here. It should matter on a larger network or fresh data,
and this run cannot say whether it does.

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
