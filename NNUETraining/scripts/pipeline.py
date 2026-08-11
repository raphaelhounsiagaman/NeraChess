#!/usr/bin/env python3
"""Runs the self-play training loop end to end.

No external engine is involved at any point. Generation 0 is trained on the
material balance of random play, which is enough to make the engine play
non-random chess; every generation after that trains on games the previous
generation played.

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
        run(
            [
                str(self.args.selfplay),
                "--network", str(self.network(generation - 1)),
                "--output", str(output),
                "--positions", str(self.args.positions),
                "--depth", str(self.args.depth),
                "--threads", str(self.args.threads),
                "--hash", str(self.args.hash_megabytes),
                "--seed", str(self.args.seed + generation),
                "--quiet",
            ],
            f"generation {generation} data (self-play with gen{generation - 1})",
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

    def run_all(self) -> None:
        self.generate_material()
        self.train(0)
        self.verify(0)

        for generation in range(1, self.args.generations + 1):
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
                        help="self-play generations after the material bootstrap")

    parser.add_argument("--selfplay", type=Path, default=DEFAULT_SELFPLAY)
    parser.add_argument("--engine", type=Path, default=DEFAULT_UCI)

    parser.add_argument("--bootstrap-positions", type=int, default=1_000_000,
                        help="material-labelled positions for generation 0")
    parser.add_argument("--positions", type=int, default=2_000_000,
                        help="self-play positions per generation")
    parser.add_argument("--depth", type=int, default=6,
                        help="search depth per self-play move")
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
