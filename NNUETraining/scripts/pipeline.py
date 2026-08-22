#!/usr/bin/env python3
"""Runs the self-play training loop end to end.

No external engine is involved in *this* loop: generation 0 is trained on the
material balance of random play, which is enough to make the engine play
non-random chess, and every generation after that trains on games the previous
generation played, labelled with its own search.

That is not how the shipped network was labelled -- see docs/MODEL_CARD.md.
Self-play labelling has a ceiling, because a network trained on its own search
can only chase itself; generation 42 is roughly where it stopped improving.

    python3 scripts/pipeline.py --workdir runs/first --generations 5

Each generation writes ``genN.txt`` (samples) and ``genN.nnue`` (network) into
the work directory, so a run can be stopped and resumed: existing files are
reused rather than regenerated.

Needs PyTorch for the training step. Build the engine first::

    make -C NeraChessSelfPlay config=release
    make -C NeraChessUCI config=release
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from nnue_training import architecture, serialize  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_SELFPLAY = REPO_ROOT / "bin/Release/NeraChessSelfPlay/NeraChessSelfPlay"
DEFAULT_UCI = REPO_ROOT / "bin/Release/NeraChessUCI/NeraChessUCI"


def run(command: list[str], description: str) -> None:
    print(f"\n=== {description} ===\n$ {' '.join(str(part) for part in command)}", flush=True)
    started = time.monotonic()
    result = subprocess.run(command)
    if result.returncode != 0:
        raise SystemExit(f"{description} failed with exit code {result.returncode}")
    print(f"({time.monotonic() - started:.1f}s)", flush=True)


class Pipeline:
    def __init__(self, arguments: argparse.Namespace) -> None:
        self.args = arguments
        self.workdir = Path(arguments.workdir)
        self.workdir.mkdir(parents=True, exist_ok=True)

        for tool in (arguments.selfplay, arguments.engine):
            if not Path(tool).exists():
                raise SystemExit(
                    f"{tool} not found. Build it first, for example:\n"
                    f"  make -C NeraChessSelfPlay config=release"
                )

    def samples(self, generation: int) -> Path:
        return self.workdir / f"gen{generation}.txt"

    def network(self, generation: int) -> Path:
        return self.workdir / f"gen{generation}.nnue"

    def generate_material(self) -> None:
        """Generation 0's data: random play labelled with material balance."""
        output = self.samples(0)
        if output.exists() and not self.args.force:
            print(f"reusing {output}")
            return
        run(
            [
                str(self.args.selfplay),
                "--mode", "material",
                "--output", str(output),
                "--positions", str(self.args.bootstrap_positions),
                "--threads", str(self.args.threads),
                "--seed", str(self.args.seed),
                "--quiet",
            ],
            "generation 0 data (material labels)",
        )

    def generate_selfplay(self, generation: int) -> None:
        output = self.samples(generation)
        if output.exists() and not self.args.force:
            print(f"reusing {output}")
            return
        budget = (["--nodes", str(self.args.nodes)] if self.args.nodes
                  else ["--depth", str(self.args.depth)])
        run(
            [
                str(self.args.selfplay),
                "--network", str(self.network(generation - 1)),
                "--output", str(output),
                "--positions", str(self.args.positions),
                *budget,
                "--threads", str(self.args.threads),
                "--hash", str(self.args.hash_megabytes),
                "--seed", str(self.args.seed + generation),
                "--quiet",
            ],
            f"generation {generation} data (self-play with gen{generation - 1})"
            f" at {'nodes ' + str(self.args.nodes) if self.args.nodes else 'depth ' + str(self.args.depth)}",
        )

    def train(self, generation: int) -> None:
        output = self.network(generation)
        if output.exists() and not self.args.force:
            print(f"reusing {output}")
            return

        # Generation 0 is fitting a known function, so it trains purely on the
        # score. Later generations blend in the game result, which is noisier
        # per position but unbiased.
        bootstrap = generation == 0
        command = [
            sys.executable, "-m", "nnue_training.train",
            "--data", str(self.samples(generation)),
            "--output", str(output),
            "--epochs", str(self.args.bootstrap_epochs if bootstrap else self.args.epochs),
            "--batch-size", str(self.args.batch_size),
            "--learning-rate", str(self.args.bootstrap_learning_rate if bootstrap
                                   else self.args.learning_rate),
            "--lambda-start", "1.0",
            "--lambda-end", "1.0" if bootstrap else str(self.args.lambda_end),
            "--device", self.args.device,
            "--seed", str(self.args.seed),
            "--no-checkpoints",
        ]
        run(command, f"training generation {generation}")

    def verify(self, generation: int) -> None:
        """Requires the engine to evaluate the new network exactly as the trainer does."""
        run(
            [
                sys.executable, "-m", "nnue_training.verify",
                "--network", str(self.network(generation)),
                "--engine", str(self.args.engine),
            ],
            f"verifying generation {generation}",
        )

    def prepare_resume(self, start: int) -> None:
        """Checks that the network generation `start` will build on is usable.

        Failing here costs a second. Failing later, after hours of self-play,
        costs the run.
        """
        previous = self.network(start - 1)

        if not previous.exists() and self.args.seed_network:
            source = Path(self.args.seed_network)
            if not source.exists():
                raise SystemExit(f"--seed-network {source} does not exist")
            previous.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, previous)
            print(f"seeded {previous} from {source}")

        if not previous.exists():
            raise SystemExit(
                f"resuming at generation {start} needs {previous}.\n"
                f"Either put the network there, or pass "
                f"--seed-network <path to a .nnue> to copy one in."
            )

        # Reject a truncated or wrong-architecture network now rather than
        # after the first generation of games has been played against it.
        try:
            header, _ = serialize.read(previous)
        except Exception as error:  # noqa: BLE001 - surfaced verbatim below
            raise SystemExit(f"{previous} is not a usable network: {error}") from error

        print(f"resuming from {previous} ({architecture.describe()})")
        assert header.architecture_hash == architecture.architecture_hash()

    def run_all(self) -> None:
        start = max(0, self.args.start_generation)

        if self.args.generations < max(start, 1):
            raise SystemExit(
                f"--generations {self.args.generations} is below the starting "
                f"generation {max(start, 1)}; nothing to do. --generations is the "
                f"last generation number to produce, not a count."
            )

        if start == 0:
            self.generate_material()
            self.train(0)
            self.verify(0)
            start = 1
        else:
            self.prepare_resume(start)

        for generation in range(start, self.args.generations + 1):
            self.generate_selfplay(generation)
            self.train(generation)
            self.verify(generation)

        final = self.network(self.args.generations)
        print(f"\nfinal network: {final}")
        if self.args.install:
            destination = Path(self.args.install)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(final, destination)
            print(f"installed to {destination}")


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="pipeline.py",
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--workdir", type=Path, default=Path("runs/default"))
    parser.add_argument("--generations", type=int, default=3,
                        help="last generation number to produce, not a count")
    parser.add_argument("--start-generation", type=int, default=0,
                        help="resume here instead of bootstrapping; needs the "
                             "previous generation's network in the work directory")
    parser.add_argument("--seed-network", type=Path,
                        help="copy this network in as the starting point when "
                             "resuming, e.g. an existing nera.nnue")

    parser.add_argument("--selfplay", type=Path, default=DEFAULT_SELFPLAY)
    parser.add_argument("--engine", type=Path, default=DEFAULT_UCI)

    parser.add_argument("--bootstrap-positions", type=int, default=1_000_000,
                        help="material-labelled positions for generation 0")
    parser.add_argument("--positions", type=int, default=2_000_000,
                        help="self-play positions per generation")
    # Mutually exclusive: NeraChessSelfPlay lets --nodes override --depth, and
    # a run that passed both would record a depth it did not use.
    budget = parser.add_mutually_exclusive_group()
    budget.add_argument("--depth", type=int, default=6,
                        help="search depth per self-play move")
    budget.add_argument("--nodes", type=int, default=None,
                        help="node budget per self-play move instead of a depth; "
                             "makes the work per move predictable regardless of "
                             "how sharp the position is")
    parser.add_argument("--threads", type=int, default=0,
                        help="parallel game workers, 0 for all hardware threads")
    parser.add_argument("--hash-megabytes", type=int, default=16)

    parser.add_argument("--bootstrap-epochs", type=int, default=6)
    parser.add_argument("--bootstrap-learning-rate", type=float, default=3e-3)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=8192)
    parser.add_argument("--learning-rate", type=float, default=1e-3)
    parser.add_argument("--lambda-end", type=float, default=0.7)
    parser.add_argument("--device", default="cpu", help="cpu, cuda, or mps")

    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--force", action="store_true",
                        help="regenerate and retrain even when files already exist")
    parser.add_argument("--install", type=Path,
                        help="copy the final network here, e.g. "
                             "NeraChessApp/Resources/NNUE/nera.nnue")

    arguments = parser.parse_args(argv)
    if arguments.threads == 0:
        import os
        arguments.threads = os.cpu_count() or 1
    return arguments


def main(argv: list[str] | None = None) -> int:
    Pipeline(parse_arguments(argv)).run_all()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
