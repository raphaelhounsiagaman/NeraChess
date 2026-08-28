# NeraChess strength testing

The manually triggered GitHub Actions workflow compares one exact NeraChess
revision against another and plays until the answer is clear. GitHub supplies
temporary Ubuntu machines; no server or self-hosted runner is required.

## Test design

The test runs in **stages**. Each stage is four GitHub-hosted jobs playing in
parallel; between stages the workflow combines everything played so far and
decides whether another stage would tell it anything.

- Every opening is played with colors reversed, and results are counted in
  pairs rather than single games.
- Each stage draws its own block of opening seeds, so no stage replays the
  openings an earlier one already played.
- Each engine gets one search thread and 128 MiB of hash.
- NeraChess's internal opening book is disabled.
- The external positions come from `UHO_4060_v4.epd`.
- Candidate and baseline are compiled independently on each shard.
- The bundled NNUE files must match unless a network change is intentional.

Stages get longer as the test goes on, so an obvious change is cheap and a
small one still accumulates games at a reasonable rate:

| Stage | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Games | 1,000 | 1,000 | 2,000 | 2,000 | 4,000 | 4,000 | 4,000 |
| Roughly | 70 min | 70 min | 2.3 h | 2.3 h | 4.6 h | 4.6 h | 4.6 h |

The Fastchess revision, opening download, and opening archive checksum are
pinned in `scripts/strength/Prepare-Tools.sh`. PGNs, logs, configurations, and
the combined summary are retained as workflow artifacts for 30 days.

## The stopping rule

"Keep playing until the confidence interval excludes zero" is not a rule a test
can actually follow. If the two engines are genuinely equal the interval never
excludes zero, so the test never ends; and an interval consulted over and over
crosses zero eventually by chance alone, so a test that stops the first time it
does would call far more than 5% of neutral changes an improvement.

The workflow therefore runs a **sequential probability ratio test**, the same
instrument Fishtest uses, which is the disciplined version of the same idea. It
weighs two hypotheses:

- **H0** — the change is worth 0 Elo or less.
- **H1** — the change is worth at least `elo1`, 5 Elo by default.

After each stage it computes the log likelihood ratio between them from the
pentanomial pair counts and stops when that ratio passes either boundary,
accepting one hypothesis or the other with a 5% error rate on each side. That
error rate holds *despite* looking after every stage, which is exactly what an
interval cannot promise.

Roughly what this costs, by how large the change really is:

| True strength change | Typical games to a verdict |
|---|---:|
| +65 Elo | 1,000 (one stage) |
| +30 Elo | 1,000 (one stage) |
| +12 Elo | ~3,600 |
| 0 Elo | ~2,800, rejected |
| +5 Elo | ~8,400, and genuinely ambiguous |

A change worth exactly `elo1` sits on the boundary between the hypotheses and is
the slowest case by design. A test that reaches `max_games` without a verdict
reports as inconclusive, which is a real answer: the change is too small to
measure at this time control.

## Running a test

1. Push the candidate branch to GitHub.
2. Open **Actions -> Strength test -> Run workflow**.
3. Keep **Use workflow from** set to `main`.
4. Enter the candidate branch under `candidate_ref`.
5. Leave `baseline_ref` as `main`.
6. Leave the time control at `10+0.1` initially.
7. Enable the network-change option only when the NNUE file itself is the
   intended experiment.

The prepare job resolves both names to immutable commit hashes. A later change
to either branch cannot alter a match that has already started.

### Inputs

| Input | Default | |
|---|---|---|
| `candidate_ref` | | Revision under test |
| `baseline_ref` | `main` | Revision to beat |
| `time_control` | `10+0.1` | Fastchess time control |
| `games` | `0` | Play exactly this many games and skip the sequential test |
| `max_games` | `12000` | Ceiling on a sequential test |
| `elo1` | `5` | Improvement the test is powered to detect |
| `allow_network_change` | `false` | Permit differing NNUE files |

Set `games` to a number to get the old behaviour: a fixed-length test that plays
everything asked of it and reports the estimate and interval without stopping
early. `games=1000` reproduces exactly what the workflow used to do. This is the
right choice when the point is to measure something rather than to decide it —
comparing two networks, or putting a number on a change already known to help.

Lower `elo1` to hunt for smaller gains, at a steep price in games: the cost of a
sequential test rises roughly with the square of the effect it must resolve, so
`elo1=2` is around six times the games of `elo1=5`.

Raise `max_games` when a result matters enough to pay for it. The ceiling is
18,000 games; beyond that, run at a longer time control instead, which buys more
information per game than more games at 10+0.1.

## Reading the result

Each stage appends a running summary to the workflow summary page, so a test in
flight can be watched. The verdicts are:

- **Accepted: improvement** — H1 accepted. This clears the bar for merging.
- **Rejected: not an improvement** — H0 accepted. The change is not worth
  `elo1`; it may still be neutral rather than harmful.
- **Undecided, playing on** — no boundary crossed yet; another stage follows.
- **Inconclusive verdicts** (`Likely improvement`, `Promising, but
  inconclusive`, `No clear difference`, and so on) — reported when a fixed
  length test finishes or a sequential one hits `max_games`. These describe the
  games played and carry no error guarantee.

The estimate and interval are always reported, because they say *how large* a
change is where the verdict only says whether it is real. Do not read the
interval as a test in its own right: in a sequential run it has been consulted
repeatedly and is narrower than it looks.

## Local use

The scripts also work on Linux outside GitHub Actions:

```bash
bash scripts/strength/Prepare-Tools.sh /tmp/strength-tools
bash scripts/strength/Build-Engine.sh /path/to/candidate /tmp/candidate-build
bash scripts/strength/Build-Engine.sh /path/to/baseline /tmp/baseline-build

FASTCHESS_BIN=/tmp/strength-tools/bin/fastchess \
STRENGTH_MAX_PAIRS=125 \
STRENGTH_SEED=1001 \
bash scripts/strength/Run-Match.sh \
  /tmp/candidate-build \
  /tmp/baseline-build \
  /tmp/strength-tools/books/UHO_4060_v4.epd \
  /tmp/strength-results

python3 scripts/strength/Summarize-Results.py /tmp/strength-results
```

Run more batches into sibling directories under one parent and point
`Summarize-Results.py` at the parent; it combines every `fastchess.log` beneath
whatever directory it is given. Use a different `STRENGTH_SEED` for each, or
they will replay the same openings and the extra games will add no information.

Use fresh output directories for every run so old binaries and PGNs cannot be
mistaken for the current experiment.

The statistics have their own tests:

```bash
python3 -m unittest discover -s scripts/strength/tests -t scripts/strength/tests
```
