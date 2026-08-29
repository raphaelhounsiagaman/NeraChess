"""Cross-checks a network file against the engine that will run it.

The most expensive NNUE bug to find is a silent mismatch between the trainer's
idea of the network and the engine's: training converges, the exported file
loads, and the engine simply plays badly. This module makes that failure loud
by evaluating the same positions two ways -- with this package's reference
forward pass over the quantized parameters, and with ``NeraChessUCI``'s
``eval`` command -- and requiring the answers to be identical.

Usage::

    python -m nnue_training.verify \\
        --network nera.nnue \\
        --engine ./bin/Release/NeraChessUCI/NeraChessUCI

Standard library only; no PyTorch needed.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from . import architecture as arch
from . import features as feat
from . import quantize as quant
from . import serialize
from .engine import UciEngine

#: Positions covering an opening, a lone-pawn endgame and its mirror, a dense
#: middlegame, and an unbalanced endgame -- plus every combination of the two
#: horizontal orientations, since a disagreement about which side of the d/e
#: boundary a king is on would otherwise show up only for some positions.
#:
#: The last five carry the kings through the input buckets the first eight do
#: not reach, for the same reason: the engine and the trainer each choose a
#: bucket independently, and a disagreement about one of them is invisible
#: until a king actually stands there.
DEFAULT_POSITIONS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1",
    "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "2kr3r/pp3ppp/8/8/8/8/PPP2PPP/2KR3R w - - 0 1",
    "r6k/1p6/8/8/3K4/8/6P1/R7 w - - 0 1",
    "3k2r1/4pp1p/8/8/8/1N6/PP4PP/R4K2 b - - 0 1",
    "4k3/8/8/8/8/8/4K3/8 w - - 0 1",
    "8/7k/8/8/8/8/6K1/8 w - - 0 1",
    "8/8/8/1k6/6K1/8/8/8 w - - 0 1",
    "8/8/8/8/8/3k4/8/1K6 w - - 0 1",
    "8/7K/8/8/8/8/k7/8 w - - 0 1",
]


@dataclass
class Comparison:
    fen: str
    reference: int
    engine: int

    @property
    def agrees(self) -> bool:
        return self.reference == self.engine


def reference_evaluation(parameters: serialize.NetworkParameters, fen: str) -> int:
    """Evaluates a FEN with the reference integer forward pass."""
    own, their = feat.both_perspectives(fen)
    return quant.forward(parameters, own, their)


def compare(
    network_path: Path, engine_path: Path, positions: list[str]
) -> list[Comparison]:
    header, parameters = serialize.read(network_path)
    if header.architecture_hash != arch.architecture_hash():
        raise SystemExit(
            f"{network_path} was built for a different architecture than "
            f"{arch.describe()}"
        )

    comparisons: list[Comparison] = []
    with UciEngine(engine_path, eval_file=network_path) as engine:
        for fen in positions:
            comparisons.append(
                Comparison(
                    fen=fen,
                    reference=reference_evaluation(parameters, fen),
                    engine=engine.evaluate(fen),
                )
            )
    return comparisons


def report(comparisons: list[Comparison]) -> int:
    """Prints the comparison table and returns a process exit code."""
    disagreements = 0
    for comparison in comparisons:
        marker = "ok  " if comparison.agrees else "FAIL"
        print(
            f"{marker} reference {comparison.reference:>7}  "
            f"engine {comparison.engine:>7}  {comparison.fen}"
        )
        if not comparison.agrees:
            disagreements += 1

    if disagreements:
        print(
            f"\n{disagreements} of {len(comparisons)} positions disagree. "
            "The trainer and the engine do not implement the same network."
        )
        return 1

    print(f"\nall {len(comparisons)} positions agree")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="nnue_training.verify",
        description="Checks that the engine evaluates a network exactly as the trainer does.",
    )
    parser.add_argument("--network", type=Path, required=True)
    parser.add_argument(
        "--engine",
        type=Path,
        default=Path("bin/Release/NeraChessUCI/NeraChessUCI"),
    )
    parser.add_argument(
        "--fen",
        action="append",
        dest="fens",
        help="extra position to check; repeatable",
    )
    arguments = parser.parse_args(argv)

    positions = list(DEFAULT_POSITIONS) + list(arguments.fens or [])
    return report(compare(arguments.network, arguments.engine, positions))


if __name__ == "__main__":
    raise SystemExit(main())
