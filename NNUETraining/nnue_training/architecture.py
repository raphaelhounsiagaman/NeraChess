"""Network shape, mirrored from NeraChessNNUE/src/NetworkArchitecture.h.

The C++ engine and this trainer must agree on every constant here. They are
duplicated rather than generated because the C++ side needs them at compile
time; ``tests/test_architecture.py`` checks that the two copies still match by
comparing :func:`architecture_hash` against the value the engine emitted into
``tests/feature_vectors.json``.

Current shape::

    (768 -> 512)x2 -> 1

Changing anything in this module without changing the header (or the reverse)
makes every network this trainer exports fail to load, which is the intended
outcome: a silent mismatch would produce an engine that evaluates nonsense.
"""

from __future__ import annotations

from enum import IntEnum

# -- Input features -------------------------------------------------------

SQUARE_COUNT = 64
PIECE_TYPE_COUNT = 6
PERSPECTIVE_COUNT = 2

#: What a feature index means, independent of how many there are.
#:
#: 1. ``(relative colour, piece type, relative square)``; king independent.
#: 2. Adds horizontal mirroring: a perspective's squares are reflected when its
#:    own king stands on files a-d, so that its king is always seen on files
#:    e-h.
#:
#: Bumped whenever a feature index comes to mean something new while every
#: dimension stays the same. Mixed into :func:`architecture_hash`, so a network
#: trained under an older feature set is rejected by the engine rather than
#: read as though nothing changed. Must match
#: ``NeraChessNNUE::Architecture::FeatureSetVersion``.
FEATURE_SET_VERSION = 2

#: Features per perspective: (relative colour, piece type, canonical square).
PERSPECTIVE_INPUT_SIZE = PERSPECTIVE_COUNT * PIECE_TYPE_COUNT * SQUARE_COUNT

#: Feature-transformer matrices selected by the perspective's own king square.
#: One bucket means every king square indexes the same matrix; a king move can
#: still force a refresh of its own half by flipping its horizontal
#: orientation.
INPUT_BUCKET_COUNT = 1

TOTAL_INPUT_SIZE = INPUT_BUCKET_COUNT * PERSPECTIVE_INPUT_SIZE

# -- Hidden layer ---------------------------------------------------------

#: Accumulator width per perspective. The output layer sees 2 * HIDDEN_SIZE.
HIDDEN_SIZE = 512

OUTPUT_INPUT_SIZE = PERSPECTIVE_COUNT * HIDDEN_SIZE

# -- Output layer ---------------------------------------------------------

OUTPUT_BUCKET_COUNT = 1

# -- Quantization ---------------------------------------------------------

#: Scale for feature weights, feature biases, and accumulator values.
#: Doubles as the clipping ceiling of the activation.
QUANTIZATION_A = 255

#: Scale for output weights.
QUANTIZATION_B = 64

#: Maps the network's win-probability-like output onto centipawns.
EVAL_SCALE = 400


class Activation(IntEnum):
    CLIPPED_RELU = 0
    SQUARED_CLIPPED_RELU = 1


ACTIVATION = Activation.SQUARED_CLIPPED_RELU

# -- Derived sizes --------------------------------------------------------

FEATURE_WEIGHT_COUNT = TOTAL_INPUT_SIZE * HIDDEN_SIZE
FEATURE_BIAS_COUNT = HIDDEN_SIZE
OUTPUT_WEIGHT_COUNT = OUTPUT_BUCKET_COUNT * OUTPUT_INPUT_SIZE
OUTPUT_BIAS_COUNT = OUTPUT_BUCKET_COUNT

TOTAL_PARAMETER_COUNT = (
    FEATURE_WEIGHT_COUNT
    + FEATURE_BIAS_COUNT
    + OUTPUT_WEIGHT_COUNT
    + OUTPUT_BIAS_COUNT
)

#: Every parameter is a little-endian int16.
PARAMETER_BYTES = 2
TOTAL_PARAMETER_BYTES = TOTAL_PARAMETER_COUNT * PARAMETER_BYTES


def architecture_hash() -> int:
    """FNV-1a over the architecture constants.

    Must reproduce ``NeraChessNNUE::Architecture::ArchitectureHash()`` exactly;
    the value is embedded in every exported network so the engine can reject a
    file trained for a different shape.
    """
    prime = 16_777_619
    mask = 0xFFFFFFFF
    digest = 2_166_136_261

    def mix(value: int) -> None:
        nonlocal digest
        for byte in range(8):
            digest ^= (value >> (byte * 8)) & 0xFF
            digest = (digest * prime) & mask

    mix(FEATURE_SET_VERSION)
    mix(PERSPECTIVE_INPUT_SIZE)
    mix(INPUT_BUCKET_COUNT)
    mix(HIDDEN_SIZE)
    mix(OUTPUT_BUCKET_COUNT)
    mix(QUANTIZATION_A)
    mix(QUANTIZATION_B)
    mix(EVAL_SCALE)
    mix(int(ACTIVATION))
    return digest


def describe() -> str:
    """Human-readable shape summary, matching the engine's info string."""
    return (
        f"{PERSPECTIVE_INPUT_SIZE}x{INPUT_BUCKET_COUNT} -> "
        f"{HIDDEN_SIZE}x2 -> 1x{OUTPUT_BUCKET_COUNT} "
        f"(qa {QUANTIZATION_A}, qb {QUANTIZATION_B}, scale {EVAL_SCALE})"
    )
