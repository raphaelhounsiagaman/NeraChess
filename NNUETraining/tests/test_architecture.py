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

FIXTURE = Path(__file__).parent / "feature_vectors.json"


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
        # The engine stores feature indices as uint16_t.
        self.assertLessEqual(arch.TOTAL_INPUT_SIZE, 65535)

    def test_quantization_constants_are_positive(self) -> None:
        self.assertGreater(arch.QUANTIZATION_A, 0)
        self.assertGreater(arch.QUANTIZATION_B, 0)
        self.assertGreater(arch.EVAL_SCALE, 0)

    def test_bucket_counts_are_positive(self) -> None:
        self.assertGreaterEqual(arch.INPUT_BUCKET_COUNT, 1)
        self.assertGreaterEqual(arch.OUTPUT_BUCKET_COUNT, 1)


if __name__ == "__main__":
    unittest.main()
