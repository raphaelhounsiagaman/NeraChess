"""Network shape, mirrored from NeraChessNNUE/src/NetworkArchitecture.h.

The C++ engine and this trainer must agree on every constant here. They are
duplicated rather than generated because the C++ side needs them at compile
time; ``tests/test_architecture.py`` checks that the two copies still match by
comparing :func:`architecture_hash` against the value the engine emitted into
``tests/feature_vectors.json``.

Current shape::

    (768x8 -> 512)x2 -> 1x8

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
#: 3. Adds king buckets: the canonical square of a perspective's own king
#:    selects which feature-transformer matrix its features index, via
#:    :data:`nnue_training.features.KING_BUCKET_TABLE`.
#:
#: Bumped whenever a feature index comes to mean something new while every
#: dimension stays the same. Mixed into :func:`architecture_hash`, so a network
#: trained under an older feature set is rejected by the engine rather than
#: read as though nothing changed. Must match
#: ``NeraChessNNUE::Architecture::FeatureSetVersion``.
#:
#: Note that INPUT_BUCKET_COUNT is a dimension and is caught on its own. This
#: version is what distinguishes two *layouts* with the same bucket count.
FEATURE_SET_VERSION = 3

#: Features per perspective: (relative colour, piece type, canonical square).
PERSPECTIVE_INPUT_SIZE = PERSPECTIVE_COUNT * PIECE_TYPE_COUNT * SQUARE_COUNT

#: Feature-transformer matrices selected by the perspective's own king square.
#: A king move forces a refresh of its own half whenever it changes that half's
#: bucket, on top of the refresh it already forces by flipping the half's
#: horizontal orientation. Mirroring canonicalizes the own king onto files e-h,
#: so the mapping's domain is 32 squares;
#: :data:`nnue_training.features.KING_BUCKET_TABLE` is what divides them.
INPUT_BUCKET_COUNT = 8

TOTAL_INPUT_SIZE = INPUT_BUCKET_COUNT * PERSPECTIVE_INPUT_SIZE

# -- Hidden layer ---------------------------------------------------------

#: Accumulator width per perspective. The output layer sees 2 * HIDDEN_SIZE.
HIDDEN_SIZE = 512

OUTPUT_INPUT_SIZE = PERSPECTIVE_COUNT * HIDDEN_SIZE

# -- Output layer ---------------------------------------------------------

#: Output heads selected by the total number of pieces on the board. Only the
#: head changes: a position still activates the same features and fills the
#: same accumulator, so the extra heads cost 8 x 1024 weights and nothing at
#: all per evaluation. :data:`OUTPUT_BUCKET_TABLE` is what divides the piece
#: counts, and this must equal the number of distinct values in it.
OUTPUT_BUCKET_COUNT = 8

#: What an output bucket index means, independent of how many there are.
#:
#: 1. The total number of pieces on the board, divided by
#:    :data:`OUTPUT_BUCKET_TABLE`.
#:
#: The same argument that gives :data:`FEATURE_SET_VERSION` its keep, applied
#: to the other end of the network: OUTPUT_BUCKET_COUNT is a dimension and is
#: caught on its own, but re-tuning the table without changing how many buckets
#: it uses would leave every size field identical and every output weight
#: meaning something else. Mixed into :func:`architecture_hash`. Must match
#: ``NeraChessNNUE::Architecture::OutputBucketVersion``.
OUTPUT_BUCKET_VERSION = 1

#: Total pieces on the board -> output head, mirroring
#: ``NeraChessNNUE::Network::OutputBucketTable``::
#:
#:     pieces:  2-4  5-8  9-12 13-16 17-20 21-24 25-28 29-32
#:     bucket:    0    1     2     3     4     5     6     7
#:
#: Four piece counts to a head, which divides 2..32 evenly. Indexed by the
#: count itself rather than computing ``(count - 1) // 4`` so that counts 0 and
#: 1 land in bucket 0; those are not legal positions, but the feature code
#: tolerates a board with no king and the tests build them.
OUTPUT_BUCKET_TABLE = (
    0, 0, 0, 0, 0,  # 0-4 pieces
    1, 1, 1, 1,     # 5-8
    2, 2, 2, 2,     # 9-12
    3, 3, 3, 3,     # 13-16
    4, 4, 4, 4,     # 17-20
    5, 5, 5, 5,     # 21-24
    6, 6, 6, 6,     # 25-28
    7, 7, 7, 7,     # 29-32
)

assert len(OUTPUT_BUCKET_TABLE) == 33, "the table is indexed by a piece count, 0..32"
assert set(OUTPUT_BUCKET_TABLE) == set(range(OUTPUT_BUCKET_COUNT)), (
    "OUTPUT_BUCKET_TABLE must map into [0, OUTPUT_BUCKET_COUNT) and use every bucket"
)


def output_bucket_of(piece_count: int) -> int:
    """Output head for a position with ``piece_count`` pieces on the board.

    The whole definition of the map. Mirrors
    ``NeraChessNNUE::Network::OutputBucketOfPieceCount``; the two are checked
    against each other through ``tests/feature_vectors.json``, which the engine
    writes with the bucket it chose for each position.

    In the 768 feature set the number of active features per perspective *is*
    the piece count -- one feature per piece, kings included -- so a trainer
    holding a collated batch can call this without going back to the position.
    """
    if piece_count < 0:
        raise ValueError(f"piece count {piece_count} is negative")
    return OUTPUT_BUCKET_TABLE[min(piece_count, len(OUTPUT_BUCKET_TABLE) - 1)]

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
    mix(OUTPUT_BUCKET_VERSION)
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
