#!/usr/bin/env python3
"""Tiles a one-head network across output buckets to seed a bucketed run.

Output buckets give the network one copy of the ``1024 -> 1`` output layer per
band of material instead of one layer for every position. That changes a
dimension, so ``OUTPUT_BUCKET_COUNT``, ``OUTPUT_WEIGHT_COUNT``,
``OUTPUT_BIAS_COUNT``, ``TOTAL_PARAMETER_COUNT`` and the architecture hash all
move at once and a pre-bucket network is refused by ``serialize.read``.

Like ``expand_king_buckets.py``, and unlike ``port_feature_set.py``, this **is**
an exact conversion. Give every head the same weights and the same bias and the
bucketed network computes, position for position, precisely what the one-head
network computed: the piece count selects a head, and if every head is identical
then which one it selects does not matter. So the seed does not merely start
near the source network's strength, it starts *at* it, and training's whole job
is to pull the eight heads apart.

That exactness is worth using. A bucketed engine loaded with this file must
evaluate every position identically to the pre-bucket engine loaded with the
source, which turns a subtle question -- did the bucket table, the piece count
and the weight offset all land correctly? -- into a byte comparison.

Usage::

    python3 scripts/expand_output_buckets.py \\
        --input  NeraChessApp/Resources/NNUE/nera.nnue \\
        --output /tmp/nera-ob-init.nnue

The output is a normal network for the current architecture: it loads in the
engine and it passes ``nnue_training.verify``. It is a *seed*, not a release --
every head holds the same weights, so it has none of the capacity the buckets
exist to provide until it has been trained.

Standard library only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from nnue_training import architecture as arch  # noqa: E402
from nnue_training import serialize  # noqa: E402

#: The architecture hash before output buckets: feature set 3, eight input
#: buckets, one output head, everything else as it is now.
PRE_OUTPUT_BUCKET_ARCHITECTURE_HASH = 808_742_301

#: Output heads those networks were trained with.
SOURCE_OUTPUT_BUCKET_COUNT = 1


def read_payload(data: bytes) -> serialize.NetworkParameters:
    """Parses a one-head container without relaxing ``serialize.read``.

    The header is unpacked here rather than by ``read_header`` on purpose:
    ``read_header`` refusing this file is the behaviour the rest of the system
    depends on, and loosening it so that one script can do its job would make
    every other caller quietly accept an incompatible network too.
    """
    if len(data) < serialize.HEADER_BYTES:
        raise SystemExit("input is smaller than a network header")

    fields = serialize._HEADER_STRUCT.unpack(data[: serialize.HEADER_BYTES])
    header = serialize.Header(*fields[1:])

    if fields[0] != serialize.MAGIC:
        raise SystemExit("input is not a NeraChess network")
    if header.version != serialize.VERSION:
        raise SystemExit(
            f"input uses container version {header.version}, expected "
            f"{serialize.VERSION}; this script ports architectures, not containers"
        )
    if header.architecture_hash != PRE_OUTPUT_BUCKET_ARCHITECTURE_HASH:
        raise SystemExit(
            f"input carries architecture hash {header.architecture_hash}, not the "
            f"pre-output-bucket {PRE_OUTPUT_BUCKET_ARCHITECTURE_HASH}. Tiling is only "
            "exact from that exact starting point; from anything else the output "
            "would be weights arranged to look loadable and trained for something "
            "this build does not implement."
        )
    if header.output_bucket_count != SOURCE_OUTPUT_BUCKET_COUNT:
        raise SystemExit(
            f"input already has {header.output_bucket_count} output buckets; this "
            "script expands one into many and cannot re-divide an existing map"
        )

    # Only the output-bucket dimension may differ from this build. Everything
    # else must already match, or the file is corrupt rather than older.
    expected = serialize.Header.for_current_architecture(header.checksum)
    for name in (
        "perspective_input_size",
        "input_bucket_count",
        "hidden_size",
        "quantization_a",
        "quantization_b",
        "eval_scale",
        "activation",
    ):
        if getattr(header, name) != getattr(expected, name):
            raise SystemExit(
                f"input's {name} is {getattr(header, name)}, expected "
                f"{getattr(expected, name)}; this is not a shape this build can read"
            )

    source_parameter_count = (
        arch.FEATURE_WEIGHT_COUNT
        + arch.FEATURE_BIAS_COUNT
        + SOURCE_OUTPUT_BUCKET_COUNT * arch.OUTPUT_INPUT_SIZE
        + SOURCE_OUTPUT_BUCKET_COUNT
    )
    if header.parameter_count != source_parameter_count:
        raise SystemExit(
            f"input declares {header.parameter_count} parameters, expected "
            f"{source_parameter_count} for a one-head network of this shape"
        )

    payload_bytes = header.parameter_count * arch.PARAMETER_BYTES
    payload = data[serialize.HEADER_BYTES : serialize.HEADER_BYTES + payload_bytes]
    if len(payload) != payload_bytes:
        raise SystemExit("input is truncated")
    if serialize.checksum(payload) != header.checksum:
        raise SystemExit("input failed its checksum")

    values = list(struct.unpack(f"<{header.parameter_count}h", payload))
    cursor = 0

    def take(count: int) -> list[int]:
        nonlocal cursor
        chunk = values[cursor : cursor + count]
        cursor += count
        return chunk

    # The bucket is the outermost axis of both output blocks, so giving every
    # head the same weights is a plain repetition rather than an interleave.
    # Nothing before them grows: the feature transformer never saw the output
    # bucket, so its weights and bias are copied through unchanged.
    feature_weights = take(arch.FEATURE_WEIGHT_COUNT)
    feature_bias = take(arch.FEATURE_BIAS_COUNT)
    one_head_weights = take(arch.OUTPUT_INPUT_SIZE)
    one_head_bias = take(SOURCE_OUTPUT_BUCKET_COUNT)

    return serialize.NetworkParameters(
        feature_weights=feature_weights,
        feature_bias=feature_bias,
        output_weights=one_head_weights * arch.OUTPUT_BUCKET_COUNT,
        output_bias=one_head_bias * arch.OUTPUT_BUCKET_COUNT,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--input", type=Path, required=True, help="a one-head .nnue")
    parser.add_argument(
        "--output", type=Path, required=True, help="where to write the seed"
    )
    arguments = parser.parse_args(argv)

    if arch.OUTPUT_BUCKET_COUNT <= SOURCE_OUTPUT_BUCKET_COUNT:
        raise SystemExit(
            f"this build has {arch.OUTPUT_BUCKET_COUNT} output bucket(s), so there "
            "is nothing to expand into"
        )

    parameters = read_payload(arguments.input.read_bytes())
    written = serialize.write(arguments.output, parameters)

    # Prove the result is readable through the ordinary path rather than
    # asserting it: this script's whole job is producing a file the strict
    # loader accepts.
    header, round_tripped = serialize.read(written)

    # And prove the tiling itself, which is the property the seed's usefulness
    # rests on. A head that came out different would be a silently weaker
    # network rather than a failure.
    first = round_tripped.output_weights[: arch.OUTPUT_INPUT_SIZE]
    for bucket in range(1, arch.OUTPUT_BUCKET_COUNT):
        start = bucket * arch.OUTPUT_INPUT_SIZE
        if round_tripped.output_weights[start : start + arch.OUTPUT_INPUT_SIZE] != first:
            raise SystemExit(f"head {bucket} did not come out identical to head 0")
        if round_tripped.output_bias[bucket] != round_tripped.output_bias[0]:
            raise SystemExit(f"head {bucket}'s bias did not come out identical to head 0")

    print(
        f"expanded {arguments.input} ({SOURCE_OUTPUT_BUCKET_COUNT} output bucket) "
        f"-> {written} ({arch.OUTPUT_BUCKET_COUNT} output buckets)"
    )
    print(f"  {arch.describe()}, architecture hash {header.architecture_hash}")
    print(f"  {written.stat().st_size} bytes, {header.parameter_count} parameters")
    print(
        "  every head holds the same weights, so this evaluates exactly as the "
        "source did; it is a warm-start seed, not a trained network"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
