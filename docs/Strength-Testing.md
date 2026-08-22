# NeraChess strength testing

The manually triggered GitHub Actions workflow compares one exact NeraChess
revision against another over 1,000 games. GitHub supplies temporary Ubuntu
machines; no server or self-hosted runner is required.

## Test design

- Four GitHub-hosted jobs run in parallel.
- Each job plays 125 opening pairs, or 250 games.
- Every opening is played with colors reversed.
- Each shard uses a different recorded opening seed.
- Each engine gets one search thread and 128 MiB of hash.
- NeraChess's internal opening book is disabled.
- The external positions come from `UHO_4060_v4.epd`.
- Candidate and baseline are compiled independently on each shard.
- The bundled NNUE files must match unless a network change is intentional.
- A final job combines the four Fastchess results into one estimated Elo and
  approximate paired 95% interval.

The Fastchess revision, opening download, and opening archive checksum are
pinned in `scripts/strength/Prepare-Tools.sh`. PGNs, logs, configurations, and
the combined summary are retained as workflow artifacts for 30 days.

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

## Reading the result

The combined workflow summary reports one of these screening verdicts:

- **Likely improvement:** the approximate interval is entirely above zero.
- **Promising, but inconclusive:** the estimate is at least +5 Elo, but the
  interval still includes zero.
- **No clear difference:** the estimate is between -5 and +5 Elo and its
  interval includes zero.
- **Concerning, but inconclusive:** the estimate is at most -5 Elo, but the
  interval still includes zero.
- **Likely regression:** the approximate interval is entirely below zero.

One thousand games are useful for screening obvious gains and regressions, but
not for proving small changes. A promising result should receive a longer test
before a marginal or risky search change is accepted.

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
```

Use fresh output directories for every run so old binaries and PGNs cannot be
mistaken for the current experiment.
