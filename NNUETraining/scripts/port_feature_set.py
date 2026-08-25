#!/usr/bin/env python3
"""Re-headers a feature-set-1 network so it can seed a mirrored training run.

Horizontal canonicalization (``arch.FEATURE_SET_VERSION`` 2) changed what a
feature index means without changing a single dimension, so every network
trained before it is refused by ``serialize.read`` -- correctly, because
reading those weights under the new numbering is not what they were trained
for.

There is one legitimate reason to do it anyway: **warm starting**. The old
weights are the best available starting point for the first mirrored run, and
what the new network computes from them is not nonsense, it is
``f_old(reflect(P))`` for positions whose perspective mirrors and ``f_old(P)``
for those whose perspective does not. Horizontal reflection is a true symmetry
of chess, so the first is approximately the second, and training reconciles
what is left.

So this **reinterprets, it does not convert**. There is no permutation of the
weights that makes an unmirrored network exactly right under a mirrored feature
set -- if there were, the two feature sets would be the same one.

Usage::

    python3 scripts/port_feature_set.py \\
        --input  ../NeraChessApp/Resources/NNUE/nera.nnue \\
        --output /tmp/nera-fs2-init.nnue

The output is a normal network for the current architecture: it loads in the
engine, it passes ``nnue_training.verify``, and it plays at roughly the source
network's strength. It is a *seed*, not a release -- ship a network that was
actually trained under this feature set.

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

#: The architecture hash before horizontal mirroring. Every other constant was
#: identical, so this is the only thing that distinguishes a feature-set-1
#: network from one of ours.
PRE_MIRRORING_ARCHITECTURE_HASH = 1_407_766_679

#: Feature-set version those networks were trained under.
SOURCE_FEATURE_SET_VERSION = 1


def read_payload(data: bytes) -> serialize.NetworkParameters:
    """Parses a feature-set-1 container without relaxing ``serialize.read``.

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
    if header.architecture_hash != PRE_MIRRORING_ARCHITECTURE_HASH:
        raise SystemExit(
            f"input carries architecture hash {header.architecture_hash}, not the "
            f"pre-mirroring {PRE_MIRRORING_ARCHITECTURE_HASH}. This script exists to "
            "reinterpret feature-set-1 networks and nothing else; anything else "
            "differs in a dimension, and no re-header makes those weights mean "
            "anything."
        )

    # Every dimension is unchanged across the feature-set bump, so a mismatch
    # here is a corrupt file rather than an older architecture.
    expected = serialize.Header.for_current_architecture(header.checksum)
    for name in (
        "perspective_input_size",
        "input_bucket_count",
        "hidden_size",
        "output_bucket_count",
        "quantization_a",
        "quantization_b",
        "eval_scale",
        "activation",
        "parameter_count",
    ):
        if getattr(header, name) != getattr(expected, name):
            raise SystemExit(
                f"input's {name} is {getattr(header, name)}, expected "
                f"{getattr(expected, name)}; this is not a shape this build can read"
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

    return serialize.NetworkParameters(
        feature_weights=take(arch.FEATURE_WEIGHT_COUNT),
        feature_bias=take(arch.FEATURE_BIAS_COUNT),
        output_weights=take(arch.OUTPUT_WEIGHT_COUNT),
        output_bias=take(arch.OUTPUT_BIAS_COUNT),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--input", type=Path, required=True, help="a feature-set-1 .nnue")
    parser.add_argument("--output", type=Path, required=True, help="where to write the seed")
    arguments = parser.parse_args(argv)

    parameters = read_payload(arguments.input.read_bytes())
    written = serialize.write(arguments.output, parameters)

    # Prove the result is readable through the ordinary path rather than
    # asserting it: this script's whole job is producing a file the strict
    # loader accepts.
    header, _ = serialize.read(written)
    print(
        f"ported {arguments.input} (feature set {SOURCE_FEATURE_SET_VERSION}) "
        f"-> {written} (feature set {arch.FEATURE_SET_VERSION})"
    )
    print(f"  {arch.describe()}, architecture hash {header.architecture_hash}")
    print(
        "  weights are reinterpreted, not converted: this is a warm-start seed, "
        "not a network trained under this feature set"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
