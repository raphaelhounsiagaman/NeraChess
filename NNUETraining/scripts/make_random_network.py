#!/usr/bin/env python3
"""Writes a network of deterministic pseudo-random weights.

The resulting network knows nothing about chess and will play badly. Its
purpose is to exercise the whole pipeline -- export, load, evaluate -- before
any training has happened, and to give ``nnue_training.verify`` something to
compare the engine against.

Usage::

    python3 scripts/make_random_network.py --output /tmp/random.nnue
    python3 -m nnue_training.verify --network /tmp/random.nnue \\
        --engine ../bin/Release/NeraChessUCI/NeraChessUCI

Standard library only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from nnue_training import architecture as arch  # noqa: E402
from nnue_training import serialize  # noqa: E402


def pseudo_random_values(count: int, seed: int, spread: int) -> list[int]:
    """A 64-bit LCG, so output depends only on the seed and not on the platform."""
    state = seed & 0xFFFFFFFFFFFFFFFF
    values = []
    for _ in range(count):
        state = (state * 6_364_136_223_846_793_005 + 1_442_695_040_888_963_407) % (2**64)
        values.append((state >> 33) % (2 * spread + 1) - spread)
    return values


def build(seed: int, spread: int) -> serialize.NetworkParameters:
    values = pseudo_random_values(arch.TOTAL_PARAMETER_COUNT, seed, spread)
    cursor = 0

    def take(count: int) -> list[int]:
        nonlocal cursor
        chunk = values[cursor : cursor + count]
        cursor += count
        return chunk

    return serialize.NetworkParameters(
        feature_weights=take(arch.FEATURE_WEIGHT_COUNT),
        feature_bias=take(arch.FEATURE_BIAS_COUNT),
        output_weights=take(arch.OUTPUT_WEIGHT_COUNT),
        output_bias=take(arch.OUTPUT_BIAS_COUNT),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=Path("random.nnue"))
    parser.add_argument("--seed", type=int, default=0x9E3779B97F4A7C15)
    parser.add_argument(
        "--spread",
        type=int,
        default=60,
        help="weights are drawn from [-spread, spread]; keep it small enough "
        "that a 32-piece accumulator cannot overflow int16",
    )
    arguments = parser.parse_args(argv)

    written = serialize.write(arguments.output, build(arguments.seed, arguments.spread))
    print(f"wrote {written} ({arch.describe()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
