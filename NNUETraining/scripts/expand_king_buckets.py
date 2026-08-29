#!/usr/bin/env python3
"""Tiles a one-bucket network across king buckets to seed a bucketed run.

King buckets (``arch.FEATURE_SET_VERSION`` 3) give the feature transformer one
weight matrix per region of the perspective's own king instead of one matrix
for every king square. That changes a dimension, so ``INPUT_BUCKET_COUNT``,
``FEATURE_WEIGHT_COUNT``, ``TOTAL_PARAMETER_COUNT`` and the architecture hash
all move at once and a pre-bucket network is refused by ``serialize.read``.

Unlike ``port_feature_set.py``, which reinterprets weights it cannot convert,
this **is** an exact conversion. Give every bucket the same matrix and the
bucketed network computes, position for position, precisely what the
one-bucket network computed: a feature index is ``bucket * 768 + within``, and
if the 768-row block is identical for every bucket then the bucket term
selects the same row whatever it is. So the seed does not merely start near
the source network's strength, it starts *at* it, and training's whole job is
to pull the eight copies apart.

That exactness is worth using. A bucketed engine loaded with this file must
evaluate every position identically to the pre-bucket engine loaded with the
source, which turns a subtle question -- did the bucket map, the
canonicalization and the accumulator refresh all land correctly? -- into a
byte comparison.

Usage::

    python3 scripts/expand_king_buckets.py \\
        --input  /srv/nerachess/models/prod/nera.nnue \\
        --output /tmp/nera-fs3-init.nnue

The output is a normal network for the current architecture: it loads in the
engine and it passes ``nnue_training.verify``. It is a *seed*, not a release --
every bucket holds the same weights, so it has none of the capacity the
buckets exist to provide until it has been trained.

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

#: The architecture hash before king buckets: horizontally mirrored features,
#: one input bucket, everything else as it is now.
PRE_KING_BUCKET_ARCHITECTURE_HASH = 1_184_502_749

#: Feature-set version those networks were trained under.
SOURCE_FEATURE_SET_VERSION = 2

#: Input buckets they were trained with.
SOURCE_INPUT_BUCKET_COUNT = 1

#: Feature-transformer weights one bucket holds. The source file has exactly
#: one of these blocks; the output repeats it INPUT_BUCKET_COUNT times.
WEIGHTS_PER_BUCKET = arch.PERSPECTIVE_INPUT_SIZE * arch.HIDDEN_SIZE


def read_payload(data: bytes) -> serialize.NetworkParameters:
    """Parses a one-bucket container without relaxing ``serialize.read``.

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
            f"{serialize.VERSION}; this script ports feature sets, not containers"
        )
    if header.architecture_hash != PRE_KING_BUCKET_ARCHITECTURE_HASH:
        raise SystemExit(
            f"input carries architecture hash {header.architecture_hash}, not the "
            f"pre-king-bucket {PRE_KING_BUCKET_ARCHITECTURE_HASH}. Tiling is only "
            "exact from that exact starting point; from anything else the output "
            "would be weights arranged to look loadable and trained for something "
            "this build does not implement."
        )
    if header.input_bucket_count != SOURCE_INPUT_BUCKET_COUNT:
        raise SystemExit(
            f"input already has {header.input_bucket_count} input buckets; this "
            "script expands one into many and cannot re-divide an existing map"
        )

    # Only the input-bucket dimension may differ from this build. Everything
    # else must already match, or the file is corrupt rather than older.
    expected = serialize.Header.for_current_architecture(header.checksum)
    for name in (
        "perspective_input_size",
        "hidden_size",
        "output_bucket_count",
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
        WEIGHTS_PER_BUCKET
        + arch.FEATURE_BIAS_COUNT
        + arch.OUTPUT_WEIGHT_COUNT
        + arch.OUTPUT_BIAS_COUNT
    )
    if header.parameter_count != source_parameter_count:
        raise SystemExit(
            f"input declares {header.parameter_count} parameters, expected "
            f"{source_parameter_count} for a one-bucket network of this shape"
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

    # The bucket is the outermost axis of the feature-weight block, so giving
    # every bucket the same matrix is a plain repetition of the source block
    # rather than an interleave. Nothing else grows: the feature bias is
    # shared across buckets by construction, and the output layer never saw
    # the bucket at all.
    one_bucket = take(WEIGHTS_PER_BUCKET)
    return serialize.NetworkParameters(
        feature_weights=one_bucket * arch.INPUT_BUCKET_COUNT,
        feature_bias=take(arch.FEATURE_BIAS_COUNT),
        output_weights=take(arch.OUTPUT_WEIGHT_COUNT),
        output_bias=take(arch.OUTPUT_BIAS_COUNT),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--input", type=Path, required=True, help="a one-bucket .nnue"
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="where to write the seed"
    )
    arguments = parser.parse_args(argv)

    if arch.INPUT_BUCKET_COUNT <= SOURCE_INPUT_BUCKET_COUNT:
        raise SystemExit(
            f"this build has {arch.INPUT_BUCKET_COUNT} input bucket(s), so there "
            "is nothing to expand into"
        )

    parameters = read_payload(arguments.input.read_bytes())
    written = serialize.write(arguments.output, parameters)

    # Prove the result is readable through the ordinary path rather than
    # asserting it: this script's whole job is producing a file the strict
    # loader accepts.
    header, round_tripped = serialize.read(written)

    # And prove the tiling itself, which is the property the seed's usefulness
    # rests on. A bucket that came out different would be a silently weaker
    # network rather than a failure.
    first = round_tripped.feature_weights[:WEIGHTS_PER_BUCKET]
    for bucket in range(1, arch.INPUT_BUCKET_COUNT):
        start = bucket * WEIGHTS_PER_BUCKET
        if round_tripped.feature_weights[start : start + WEIGHTS_PER_BUCKET] != first:
            raise SystemExit(f"bucket {bucket} did not come out identical to bucket 0")

    print(
        f"expanded {arguments.input} (feature set {SOURCE_FEATURE_SET_VERSION}, "
        f"{SOURCE_INPUT_BUCKET_COUNT} bucket) -> {written} "
        f"(feature set {arch.FEATURE_SET_VERSION}, {arch.INPUT_BUCKET_COUNT} buckets)"
    )
    print(f"  {arch.describe()}, architecture hash {header.architecture_hash}")
    print(f"  {written.stat().st_size} bytes, {header.parameter_count} parameters")
    print(
        "  every bucket holds the same weights, so this evaluates exactly as the "
        "source did; it is a warm-start seed, not a trained network"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
