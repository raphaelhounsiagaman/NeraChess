"""Float parameters to the int16 form the engine loads.

Mirrors the conventions documented in NeraChessNNUE/src/Quantization.h::

    feature weights, feature biases   QUANTIZATION_A
    output weights                    QUANTIZATION_B
    output biases                     QUANTIZATION_A * QUANTIZATION_B

Quantization is lossy, so a network always loses a little strength here. The
usual remedy is to clip weights during training (see ``train.py``) so that
saturation, not rounding, is the binding constraint.

Standard library only: PyTorch tensors are accepted but converted to lists
before anything happens, so this module is importable and testable without it.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Sequence

from . import architecture as arch
from .serialize import NetworkParameters

INT16_MIN = -32768
INT16_MAX = 32767


@dataclass
class QuantizationReport:
    """What quantization cost, for logging next to a training run."""

    clipped_parameters: int = 0
    max_absolute_feature_weight: float = 0.0
    max_absolute_output_weight: float = 0.0

    def describe(self) -> str:
        return (
            f"clipped {self.clipped_parameters} parameters, "
            f"max |feature weight| {self.max_absolute_feature_weight:.4f}, "
            f"max |output weight| {self.max_absolute_output_weight:.4f}"
        )


def to_lists(values: Any) -> Any:
    """Converts torch tensors and numpy arrays to nested Python lists."""
    if hasattr(values, "detach"):
        values = values.detach()
    if hasattr(values, "cpu"):
        values = values.cpu()
    if hasattr(values, "tolist"):
        return values.tolist()
    return values


def quantize_value(value: float, scale: int, report: QuantizationReport) -> int:
    """Rounds half away from zero and saturates, matching Quantization::Quantize."""
    scaled = value * scale
    rounded = int(scaled + 0.5) if scaled >= 0 else int(scaled - 0.5)
    if rounded < INT16_MIN or rounded > INT16_MAX:
        report.clipped_parameters += 1
        rounded = max(INT16_MIN, min(INT16_MAX, rounded))
    return rounded


def quantize(
    feature_weights: Any,
    feature_bias: Any,
    output_weights: Any,
    output_bias: Any,
) -> tuple[NetworkParameters, QuantizationReport]:
    """Builds engine-ready parameters from a trained model's floats.

    Expected shapes, matching :class:`nnue_training.model.NnueModel`:

    ``feature_weights``  ``[HIDDEN_SIZE][TOTAL_INPUT_SIZE]``  (torch Linear order)
    ``feature_bias``     ``[HIDDEN_SIZE]``
    ``output_weights``   ``[OUTPUT_BUCKET_COUNT][OUTPUT_INPUT_SIZE]``
    ``output_bias``      ``[OUTPUT_BUCKET_COUNT]``

    The feature matrix is transposed on the way out: the engine indexes it by
    feature first so that one active feature's weights are contiguous, which is
    what makes an accumulator update a single linear pass.
    """
    report = QuantizationReport()

    feature_weights = to_lists(feature_weights)
    feature_bias = to_lists(feature_bias)
    output_weights = to_lists(output_weights)
    output_bias = to_lists(output_bias)

    if len(feature_weights) != arch.HIDDEN_SIZE:
        raise ValueError(
            f"feature weights have {len(feature_weights)} rows, "
            f"expected {arch.HIDDEN_SIZE}"
        )
    if len(feature_weights[0]) != arch.TOTAL_INPUT_SIZE:
        raise ValueError(
            f"feature weights have {len(feature_weights[0])} columns, "
            f"expected {arch.TOTAL_INPUT_SIZE}"
        )

    quantized_feature_weights: list[int] = []
    for feature in range(arch.TOTAL_INPUT_SIZE):
        for neuron in range(arch.HIDDEN_SIZE):
            value = float(feature_weights[neuron][feature])
            report.max_absolute_feature_weight = max(
                report.max_absolute_feature_weight, abs(value)
            )
            quantized_feature_weights.append(
                quantize_value(value, arch.QUANTIZATION_A, report)
            )

    quantized_feature_bias = [
        quantize_value(float(value), arch.QUANTIZATION_A, report)
        for value in feature_bias
    ]

    quantized_output_weights: list[int] = []
    for bucket in range(arch.OUTPUT_BUCKET_COUNT):
        row = output_weights[bucket]
        if len(row) != arch.OUTPUT_INPUT_SIZE:
            raise ValueError(
                f"output weight row {bucket} has {len(row)} entries, "
                f"expected {arch.OUTPUT_INPUT_SIZE}"
            )
        for value in row:
            value = float(value)
            report.max_absolute_output_weight = max(
                report.max_absolute_output_weight, abs(value)
            )
            quantized_output_weights.append(
                quantize_value(value, arch.QUANTIZATION_B, report)
            )

    # Pre-multiplied so the engine can add it straight to the layer sum.
    output_bias_scale = arch.QUANTIZATION_A * arch.QUANTIZATION_B
    quantized_output_bias = [
        quantize_value(float(value), output_bias_scale, report)
        for value in output_bias
    ]

    parameters = NetworkParameters(
        feature_weights=quantized_feature_weights,
        feature_bias=quantized_feature_bias,
        output_weights=quantized_output_weights,
        output_bias=quantized_output_bias,
    )
    parameters.validate()
    return parameters, report


def activate(value: int) -> int:
    """Hidden-layer activation on a quantized accumulator value.

    Reference implementation of ``Quantization::Activate``; used by
    :mod:`nnue_training.verify` to reproduce the engine's arithmetic exactly.
    """
    clipped = max(0, min(arch.QUANTIZATION_A, value))
    if arch.ACTIVATION == arch.Activation.SQUARED_CLIPPED_RELU:
        return clipped * clipped
    return clipped


def dequantize(weighted_sum: int, output_bias: int) -> int:
    """Weighted sum of activations to centipawns, matching ``Dequantize``."""
    activation_scale = (
        arch.QUANTIZATION_A
        if arch.ACTIVATION == arch.Activation.SQUARED_CLIPPED_RELU
        else 1
    )
    normalized = _truncating_div(weighted_sum, activation_scale) + output_bias
    return _truncating_div(
        normalized * arch.EVAL_SCALE, arch.QUANTIZATION_A * arch.QUANTIZATION_B
    )


def _truncating_div(numerator: int, denominator: int) -> int:
    """C++ integer division, which truncates toward zero rather than down."""
    quotient = abs(numerator) // abs(denominator)
    negative = (numerator < 0) != (denominator < 0)
    return -quotient if negative else quotient


def forward(
    parameters: NetworkParameters,
    own_features: Sequence[int],
    their_features: Sequence[int],
    output_bucket: int = 0,
) -> int:
    """Reference forward pass over quantized parameters.

    Reproduces the engine's integer arithmetic exactly, so a disagreement with
    ``NeraChessUCI``'s ``eval`` command means a real bug rather than a rounding
    difference. Slow by design; use it on a handful of positions, not a dataset.
    """
    accumulators = []
    for features in (own_features, their_features):
        accumulator = list(parameters.feature_bias)
        for feature in features:
            base = feature * arch.HIDDEN_SIZE
            for neuron in range(arch.HIDDEN_SIZE):
                accumulator[neuron] += parameters.feature_weights[base + neuron]
        accumulators.append(accumulator)

    weight_base = output_bucket * arch.OUTPUT_INPUT_SIZE
    total = 0
    for half, accumulator in enumerate(accumulators):
        offset = weight_base + half * arch.HIDDEN_SIZE
        for neuron, value in enumerate(accumulator):
            total += activate(value) * parameters.output_weights[offset + neuron]

    return dequantize(total, parameters.output_bias[output_bucket])
