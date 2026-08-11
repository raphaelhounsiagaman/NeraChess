#pragma once

#include "NetworkArchitecture.h"
#include "NnueCommon.h"

#include <algorithm>
#include <cstdint>

// Fixed-point conventions shared by inference and by the exporter in
// NNUETraining/nnue_training/quantize.py.
//
// A float parameter x is stored as round(x * scale):
//
//   feature weights, feature biases, accumulator values   QuantizationA
//   output weights                                        QuantizationB
//   output biases                                         QuantizationA * QuantizationB
//
// Storing the output bias pre-multiplied lets Dequantize add it directly to
// the layer sum. Every parameter is an int16_t, so the exporter must reject a
// network whose output bias exceeds roughly +-2.0 in float terms; trained
// output biases sit near zero, so this has never been a real constraint.

namespace NeraChessNNUE::Quantization
{
    // Applies the hidden-layer activation to one quantized accumulator value
    // and widens the result so the caller can accumulate without overflow.
    //
    // Squared clipped ReLU: clamp(x, 0, QuantizationA)^2, which is the
    // quantized form of clamp(x, 0, 1)^2 scaled by QuantizationA^2.
    constexpr Accumulation Activate(int32_t value)
    {
        const int32_t clipped = std::clamp(value, 0, Architecture::QuantizationA);
        if constexpr (Architecture::Activation == Architecture::ActivationKind::SquaredClippedReLU)
            return static_cast<Accumulation>(clipped) * clipped;
        else
            return static_cast<Accumulation>(clipped);
    }

    // The extra factor the activation introduces on top of the accumulator's
    // own QuantizationA scale. Squaring applies it twice; clipping once.
    inline constexpr int32_t ActivationScale =
        Architecture::Activation == Architecture::ActivationKind::SquaredClippedReLU
            ? Architecture::QuantizationA
            : 1;

    // Converts a weighted sum of activations into a centipawn score from the
    // point of view of the side to move.
    constexpr Score Dequantize(Accumulation weightedSum, int32_t outputBias)
    {
        const Accumulation normalized = weightedSum / ActivationScale + outputBias;
        const Accumulation scaled = normalized * Architecture::EvalScale /
            (static_cast<Accumulation>(Architecture::QuantizationA) * Architecture::QuantizationB);
        return static_cast<Score>(scaled);
    }

    // Quantizes a float parameter with the given scale, saturating instead of
    // wrapping. Used by tests and by tooling that builds networks in memory.
    constexpr Weight Quantize(double value, int32_t scale)
    {
        const double scaled = value * scale;
        const double rounded = scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5;
        const double clamped = std::clamp(rounded,
            static_cast<double>(INT16_MIN), static_cast<double>(INT16_MAX));
        return static_cast<Weight>(clamped);
    }
}
