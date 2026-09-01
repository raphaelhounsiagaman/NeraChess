"""Checks that this package's architecture matches the engine's.

The architecture hash is the contract between the two: it is embedded in every
exported network and the engine refuses any file whose hash differs from its
own. If this test fails, ``nnue_training/architecture.py`` and
``NeraChessNNUE/src/NetworkArchitecture.h`` have drifted apart, and every
network exported from here would be rejected by the engine.

The expected hash comes from ``feature_vectors.json``, which the engine writes
itself, so there is no third place for the value to go stale.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from nnue_training import architecture as arch
from nnue_training import features as feat

FIXTURE = Path(__file__).parent / "feature_vectors.json"


#: The hash this architecture had before horizontal mirroring, when a feature
#: was ``(relative colour, piece type, relative square)`` and nothing else. No
#: dimension changed with the mirroring, so this value is what a network from
#: before it carries in its header -- and the only thing that stops the engine
#: reading those weights under a numbering they were never trained for.
PRE_MIRRORING_ARCHITECTURE_HASH = 1_407_766_679

#: The hash this architecture had before king buckets, when horizontal
#: mirroring was the whole of the canonicalization and every king square
#: indexed the same feature-transformer matrix. Unlike the mirroring change,
#: this one moves dimensions too -- so the size fields would catch it on their
#: own -- but the hash is still what a pre-bucket network carries, and pinning
#: it is what proves the two generations cannot be confused.
PRE_KING_BUCKET_ARCHITECTURE_HASH = 1_184_502_749


class ArchitectureHashTest(unittest.TestCase):
    def test_matches_the_engine(self) -> None:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.assertEqual(
            arch.architecture_hash(),
            fixture["architectureHash"],
            "architecture.py and NetworkArchitecture.h disagree; the engine "
            "would reject every network exported from here",
        )

    def test_is_stable(self) -> None:
        self.assertEqual(arch.architecture_hash(), arch.architecture_hash())

    def test_fits_in_thirty_two_bits(self) -> None:
        self.assertGreaterEqual(arch.architecture_hash(), 0)
        self.assertLess(arch.architecture_hash(), 2**32)

    def test_an_older_feature_set_cannot_masquerade_as_this_one(self) -> None:
        for name, previous in (
            ("pre-mirroring", PRE_MIRRORING_ARCHITECTURE_HASH),
            ("pre-king-bucket", PRE_KING_BUCKET_ARCHITECTURE_HASH),
        ):
            with self.subTest(feature_set=name):
                self.assertNotEqual(
                    arch.architecture_hash(),
                    previous,
                    "the feature set changed meaning but the architecture "
                    "hash did not; networks trained under the previous "
                    "semantics would still load and evaluate nonsense",
                )

    def test_the_feature_set_version_is_part_of_the_hash(self) -> None:
        # The dimensions cannot express a change of meaning, so this is the
        # only input that can. Prove it reaches the digest at all.
        original = arch.FEATURE_SET_VERSION
        try:
            arch.FEATURE_SET_VERSION = original + 1
            self.assertNotEqual(arch.architecture_hash(), self.expected)
        finally:
            arch.FEATURE_SET_VERSION = original
        self.assertEqual(arch.architecture_hash(), self.expected)

    @property
    def expected(self) -> int:
        return json.loads(FIXTURE.read_text(encoding="utf-8"))["architectureHash"]


class ArchitectureInvariantTest(unittest.TestCase):
    def test_perspective_input_size(self) -> None:
        self.assertEqual(
            arch.PERSPECTIVE_INPUT_SIZE,
            arch.PERSPECTIVE_COUNT * arch.PIECE_TYPE_COUNT * arch.SQUARE_COUNT,
        )
        self.assertEqual(arch.PERSPECTIVE_INPUT_SIZE, 768)

    def test_hidden_size_needs_no_simd_tail(self) -> None:
        self.assertEqual(arch.HIDDEN_SIZE % 16, 0)

    def test_parameter_count_is_the_sum_of_its_parts(self) -> None:
        self.assertEqual(
            arch.TOTAL_PARAMETER_COUNT,
            arch.FEATURE_WEIGHT_COUNT
            + arch.FEATURE_BIAS_COUNT
            + arch.OUTPUT_WEIGHT_COUNT
            + arch.OUTPUT_BIAS_COUNT,
        )
        self.assertEqual(
            arch.TOTAL_PARAMETER_BYTES,
            arch.TOTAL_PARAMETER_COUNT * arch.PARAMETER_BYTES,
        )

    def test_feature_indices_fit_in_a_uint16(self) -> None:
        # The engine stores feature indices as uint16_t, which caps the input
        # buckets at 65535 // 768 == 85 however the layout divides them.
        self.assertLessEqual(arch.TOTAL_INPUT_SIZE, 65535)
        self.assertLessEqual(arch.INPUT_BUCKET_COUNT, 65535 // arch.PERSPECTIVE_INPUT_SIZE)

    def test_quantization_constants_are_positive(self) -> None:
        self.assertGreater(arch.QUANTIZATION_A, 0)
        self.assertGreater(arch.QUANTIZATION_B, 0)
        self.assertGreater(arch.EVAL_SCALE, 0)

    def test_bucket_counts_are_positive(self) -> None:
        self.assertGreaterEqual(arch.INPUT_BUCKET_COUNT, 1)
        self.assertGreaterEqual(arch.OUTPUT_BUCKET_COUNT, 1)

    def test_the_king_bucket_table_accounts_for_every_input_bucket(self) -> None:
        # INPUT_BUCKET_COUNT sizes the network; the table decides what the
        # buckets mean. A bucket the table never names would be weights
        # nothing can reach, and a bucket beyond the count would index off the
        # end of the feature-transformer matrix.
        self.assertEqual(
            set(feat.KING_BUCKET_TABLE), set(range(arch.INPUT_BUCKET_COUNT))
        )

    def test_feature_set_version_matches_the_indexer(self) -> None:
        # Version 3 mirrors horizontally *and* buckets on the own king.
        # Bumping the constant without teaching features.py the new semantics
        # -- or the reverse -- would leave the engine and the trainer agreeing
        # on a number and disagreeing on what it means. Both halves are
        # checked behaviourally rather than by reading the constant back.
        self.assertEqual(arch.FEATURE_SET_VERSION, 3)

        # Mirroring: a position and its horizontal reflection are one thing.
        queenside = feat.active_features(
            "7k/8/8/8/8/8/8/K7 w - - 0 1", feat.Perspective.WHITE
        )
        kingside = feat.active_features(
            "k7/8/8/8/8/8/8/7K w - - 0 1", feat.Perspective.WHITE
        )
        self.assertEqual(queenside, kingside)

        # King buckets: moving the own king into another bucket renumbers
        # every one of that perspective's features, including the features of
        # pieces that did not move.
        back_rank = feat.active_features(
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1", feat.Perspective.WHITE
        )
        second_rank = feat.active_features(
            "4k3/8/8/8/8/8/4K3/8 w - - 0 1", feat.Perspective.WHITE
        )
        self.assertNotEqual(
            feat.view_of(
                feat.parse_fen_pieces("4k3/8/8/8/8/8/8/4K3 w - - 0 1"),
                feat.Perspective.WHITE,
            ).input_bucket,
            feat.view_of(
                feat.parse_fen_pieces("4k3/8/8/8/8/8/4K3/8 w - - 0 1"),
                feat.Perspective.WHITE,
            ).input_bucket,
        )
        self.assertNotEqual(back_rank, second_rank)


if __name__ == "__main__":
    unittest.main()
