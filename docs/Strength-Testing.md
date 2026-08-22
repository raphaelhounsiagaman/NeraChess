# NeraChess strength testing

This system compares one exact NeraChess revision against another by playing
paired games with colors reversed. Fastchess stops the match when its SPRT has
enough evidence, or after the configured maximum number of pairs.

Strength testing is deliberately separate from normal CI. A successful build,
perft run, or benchmark does not establish an Elo gain, and a short engine
match is too noisy to be a merge gate.

## What the workflow controls

- Both revisions are compiled on the same machine with the same compiler.
- Each engine gets one search thread and 128 MiB of hash.
- NeraChess's internal opening book is disabled.
- Positions come from `UHO_4060_v4.epd` and are played once with each color.
- Opening randomization uses a recorded seed.
- The two bundled NNUE files must be identical unless the run explicitly tests
  a network change.
- The PGN, full Fastchess log, build hashes, network hashes, and test settings
  are retained as a GitHub Actions artifact.

The default SPRT tests `H0: 0 nElo` against `H1: +5 nElo`, with alpha and beta
both set to 0.05. A green result supports accepting the +5 nElo hypothesis. A
red result rejects that target; it does not necessarily prove the candidate is
weaker. An unfinished result means the maximum game count was reached without
enough evidence.

## 1. Prepare the Ubuntu host

The server also runs the NeraChess Lichess bot and a website, so the Actions
runner must not use `haloraphi`, `nerachess`, or the web-server account. The
setup script creates a dedicated unprivileged `chesstest` account and installs
a pinned Fastchess revision and the CC0 Stockfish UHO opening suite:

```bash
cd /path/to/your/development/NeraChess
sudo bash scripts/strength/Setup-Host.sh
```

Use your development clone here, not the production checkout used by the
running Lichess bot.

The runner account must not be added to the `nerachess`, web-server, or sudo
groups. Production secrets such as `/etc/nerachess/lichess.env` should be owned
by `root:nerachess` with mode `0640`, so `chesstest` cannot read them.

## 2. Register the self-hosted runner

1. Open the NeraChess repository on GitHub.
2. Select **Settings -> Actions -> Runners -> New self-hosted runner**.
3. Select **Linux** and **x64**.
4. GitHub displays current download and registration commands. Run the download
   commands in a new directory:

   ```bash
   sudo mkdir -p /opt/actions-runner/nerachess-strength
   sudo chown chesstest:chesstest /opt/actions-runner/nerachess-strength
   cd /opt/actions-runner/nerachess-strength
   ```

5. Run GitHub's displayed download and extraction commands as `chesstest`, for
   example by prefixing each command with `sudo -u chesstest`.
6. Register it with the URL and short-lived token shown by GitHub:

   ```bash
   sudo -u chesstest ./config.sh \
     --url https://github.com/raphaelhounsiagaman/NeraChess \
     --token TOKEN_SHOWN_BY_GITHUB \
     --name nerachess-strength-1 \
     --labels nerachess-strength \
     --work /data/chess/testing/actions \
     --unattended
   ```

7. Install and start the service:

   ```bash
   sudo ./svc.sh install chesstest
   sudo ./svc.sh start
   sudo ./svc.sh status
   ```

The registration token expires quickly and is only used during registration.
Do not paste it into the repository or save it in a script.

## 3. Run a test

1. Push the candidate branch to GitHub.
2. Open **Actions -> Strength test -> Run workflow**.
3. Keep **Use workflow from** set to `main`.
4. Enter the candidate branch name and `main` as the baseline.
5. Start with the defaults.

The workflow resolves both names to immutable commit hashes before compiling.
The results appear in the workflow summary; downloadable PGN and logs appear
under **Artifacts**.

For a pull request, use the PR's branch name as `candidate_ref`. If `main`
changes while a long test is running, that does not affect the match because
the baseline hash was already resolved.

## Choosing concurrency

Every concurrent game starts two engines. With the default one thread per
engine, concurrency 2 uses approximately four busy CPU threads. Begin at 2 so
the website and production bot remain responsive. Increase it only after
watching load and bot latency. Do not run NNUE training at the same time as a
strength match; changing CPU contention adds noise.

Only one strength workflow can run at once. Additional runs queue because the
workflow has a repository-wide concurrency group.

## Testing an NNUE change

By default the match aborts if the two revisions contain different
`nera.nnue` files. This catches accidental code-and-network comparisons. Enable
**Allow candidate and baseline to use different NNUE files** only when the
network itself is the intended candidate.

## Local use

The same scripts can be used without GitHub Actions:

```bash
bash scripts/strength/Build-Engine.sh /path/to/candidate /tmp/candidate-build
bash scripts/strength/Build-Engine.sh /path/to/baseline /tmp/baseline-build

FASTCHESS_BIN=/data/chess/testing/tools/bin/fastchess \
STRENGTH_CONCURRENCY=2 \
bash scripts/strength/Run-Match.sh \
  /tmp/candidate-build \
  /tmp/baseline-build \
  /data/chess/testing/books/UHO_4060_v4.epd \
  /tmp/strength-results
```

Use fresh output directories for every run. This prevents an old PGN or build
from being mistaken for the current experiment.

## Security boundary

This is a public repository and a self-hosted runner executes repository code.
The workflow is manual rather than a `pull_request` trigger, but starting it is
still authorization to compile and execute the selected candidate. Review the
candidate before starting the job.

Do not change this workflow to use `pull_request_target`, do not give its
`GITHUB_TOKEN` write permission, and do not store deployment credentials on the
runner. Stronger isolation would require an ephemeral VM or container for every
job; the dedicated account is the minimum acceptable boundary on the shared
server.
