"""Checks the ``.nnue`` container round-trips and rejects damaged files.

The engine-side equivalent is ``TestNnueNetworkFormat`` in
NeraChessTests/src/main.cpp; the two suites deliberately assert the same
properties from opposite sides of the format.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from nnue_training import architecture as arch
from nnue_training import quantize as quant
from nnue_training import serialize


def build_parameters(seed: int = 1) -> serialize.NetworkParameters:
    """Deterministic parameters spanning the int16 range without exceeding it."""
    state = seed
    values = []
    for _ in range(arch.TOTAL_PARAMETER_COUNT):
        state = (state * 6_364_136_223_846_793_005 + 1_442_695_040_888_963_407) % (2**64)
        values.append((state >> 33) % 121 - 60)

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


class HeaderTest(unittest.TestCase):
    def test_header_is_forty_eight_bytes(self) -> None:
        header = serialize.Header.for_current_architecture(checksum=0)
        self.assertEqual(len(serialize.pack_header(header)), serialize.HEADER_BYTES)

    def test_header_describes_this_architecture(self) -> None:
        header = serialize.Header.for_current_architecture()
        self.assertTrue(header.matches_current_architecture())
        self.assertEqual(header.architecture_hash, arch.architecture_hash())
        self.assertEqual(header.parameter_count, arch.TOTAL_PARAMETER_COUNT)


class RoundTripTest(unittest.TestCase):
    def test_parameters_survive_a_round_trip(self) -> None:
        parameters = build_parameters()
        header, restored = serialize.loads(serialize.dumps(parameters))

        self.assertTrue(header.matches_current_architecture())
        self.assertEqual(list(restored.feature_weights), list(parameters.feature_weights))
        self.assertEqual(list(restored.feature_bias), list(parameters.feature_bias))
        self.assertEqual(list(restored.output_weights), list(parameters.output_weights))
        self.assertEqual(list(restored.output_bias), list(parameters.output_bias))

    def test_file_size_matches_the_architecture(self) -> None:
        data = serialize.dumps(build_parameters())
        self.assertEqual(
            len(data), serialize.HEADER_BYTES + arch.TOTAL_PARAMETER_BYTES
        )

    def test_write_and_read_a_file(self) -> None:
        parameters = build_parameters(seed=7)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "test.nnue"
            serialize.write(path, parameters)
            _, restored = serialize.read(path)
        self.assertEqual(list(restored.output_bias), list(parameters.output_bias))


class RejectionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.data = serialize.dumps(build_parameters(seed=3))

    def damage(self, offset: int, value: int) -> bytes:
        damaged = bytearray(self.data)
        damaged[offset] = value
        return bytes(damaged)

    def test_rejects_wrong_magic(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.damage(0, ord("X")))

    def test_rejects_unknown_version(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.damage(8, 99))

    def test_rejects_different_hidden_size(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.damage(20, 1))

    def test_rejects_corrupted_payload(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.damage(serialize.HEADER_BYTES, 0xFF))

    def test_rejects_truncated_file(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.data[:-2])

    def test_rejects_file_shorter_than_the_header(self) -> None:
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.loads(self.data[:8])

    def test_rejects_out_of_range_parameters(self) -> None:
        parameters = serialize.NetworkParameters(
            feature_weights=[0] * arch.FEATURE_WEIGHT_COUNT,
            feature_bias=[70_000] + [0] * (arch.FEATURE_BIAS_COUNT - 1),
            output_weights=[0] * arch.OUTPUT_WEIGHT_COUNT,
            output_bias=[0] * arch.OUTPUT_BIAS_COUNT,
        )
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.dumps(parameters)

    def test_rejects_wrong_parameter_counts(self) -> None:
        parameters = serialize.NetworkParameters(
            feature_weights=[0] * (arch.FEATURE_WEIGHT_COUNT - 1),
            feature_bias=[0] * arch.FEATURE_BIAS_COUNT,
            output_weights=[0] * arch.OUTPUT_WEIGHT_COUNT,
            output_bias=[0] * arch.OUTPUT_BIAS_COUNT,
        )
        with self.assertRaises(serialize.NetworkFormatError):
            serialize.dumps(parameters)


class QuantizationTest(unittest.TestCase):
    def test_rounds_half_away_from_zero(self) -> None:
        report = quant.QuantizationReport()
        self.assertEqual(quant.quantize_value(1.0, 255, report), 255)
        self.assertEqual(quant.quantize_value(-1.0, 255, report), -255)
        self.assertEqual(quant.quantize_value(0.5 / 255, 255, report), 1)
        self.assertEqual(quant.quantize_value(-0.5 / 255, 255, report), -1)
        self.assertEqual(report.clipped_parameters, 0)

    def test_saturates_and_reports_clipping(self) -> None:
        report = quant.QuantizationReport()
        self.assertEqual(quant.quantize_value(1000.0, 255, report), 32767)
        self.assertEqual(quant.quantize_value(-1000.0, 255, report), -32768)
        self.assertEqual(report.clipped_parameters, 2)

    def test_layout_transposes_the_feature_matrix(self) -> None:
        # A single neuron lit by a single feature must land at the index the
        # engine reads for that (feature, neuron) pair.
        feature_weights = [
            [0.0] * arch.TOTAL_INPUT_SIZE for _ in range(arch.HIDDEN_SIZE)
        ]
        feature_weights[3][5] = 1.0

        parameters, _ = quant.quantize(
            feature_weights=feature_weights,
            feature_bias=[0.0] * arch.HIDDEN_SIZE,
            output_weights=[[0.0] * arch.OUTPUT_INPUT_SIZE]
            * arch.OUTPUT_BUCKET_COUNT,
            output_bias=[0.0] * arch.OUTPUT_BUCKET_COUNT,
        )

        expected_index = 5 * arch.HIDDEN_SIZE + 3
        self.assertEqual(
            parameters.feature_weights[expected_index], arch.QUANTIZATION_A
        )
        self.assertEqual(sum(parameters.feature_weights), arch.QUANTIZATION_A)

    def test_truncating_division_matches_cpp(self) -> None:
        self.assertEqual(quant._truncating_div(7, 2), 3)
        self.assertEqual(quant._truncating_div(-7, 2), -3)
        self.assertEqual(quant._truncating_div(7, -2), -3)
        self.assertEqual(quant._truncating_div(-7, -2), 3)

    def test_zero_network_evaluates_to_zero(self) -> None:
        parameters = serialize.NetworkParameters(
            feature_weights=[0] * arch.FEATURE_WEIGHT_COUNT,
            feature_bias=[0] * arch.FEATURE_BIAS_COUNT,
            output_weights=[0] * arch.OUTPUT_WEIGHT_COUNT,
            output_bias=[0] * arch.OUTPUT_BIAS_COUNT,
        )
        self.assertEqual(quant.forward(parameters, [0, 1], [2, 3]), 0)

    def test_output_bias_alone_reaches_the_score(self) -> None:
        # The output bias is stored at QUANTIZATION_A * QUANTIZATION_B, so a
        # bias of one whole unit must come out as exactly EVAL_SCALE.
        parameters = serialize.NetworkParameters(
            feature_weights=[0] * arch.FEATURE_WEIGHT_COUNT,
            feature_bias=[0] * arch.FEATURE_BIAS_COUNT,
            output_weights=[0] * arch.OUTPUT_WEIGHT_COUNT,
            output_bias=[arch.QUANTIZATION_A * arch.QUANTIZATION_B],
        )
        self.assertEqual(quant.forward(parameters, [], []), arch.EVAL_SCALE)


if __name__ == "__main__":
    unittest.main()
