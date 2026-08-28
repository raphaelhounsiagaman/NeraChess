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
//
// Squaring the clipped activation introduces a second factor of QuantizationA
// on top of the accumulator's own scale. Activate divides that factor back
// out immediately (rather than once at the end, in Dequantize) so its result
// -- and therefore the output layer's per-term product -- fits int16/int32
// instead of needing an int32/int64 intermediate. See docs/NNUE.md.

namespace NeraChessNNUE::Quantization
{
    // Applies the hidden-layer activation to one quantized accumulator value,
    // rescaled back onto the accumulator's own [0, QuantizationA] range so the
    // output layer's kernels can multiply it against a weight with a single
    // int16 x int16 -> int32 widening multiply instead of promoting through
    // int32 first.
    //
    // Squared clipped ReLU: clamp(x, 0, QuantizationA)^2 / QuantizationA,
    // the quantized form of clamp(x, 0, 1)^2. The square and the division by
    // it are both kept at uint16_t width rather than widened to uint32_t --
    // at QuantizationA <= 255 the square never exceeds 65025, so it fits
    // uint16_t exactly, and keeping it that narrow is what lets a vectorizing
    // compiler lower the whole thing to a 16-bit clamp, a 16-bit multiply
    // (the high bits of the true product are provably zero, so the low-half
    // result is exact) and a 16-bit reciprocal-multiply division, instead of
    // promoting through a 32-bit multiply and a 32-bit division.
    constexpr int32_t Activate(int32_t value)
    {
        const int32_t clipped = std::clamp(value, 0, Architecture::QuantizationA);
        if constexpr (Architecture::Activation == Architecture::ActivationKind::SquaredClippedReLU)
        {
            const uint16_t narrowed = static_cast<uint16_t>(clipped);
            const uint16_t square = static_cast<uint16_t>(narrowed * narrowed);
            return static_cast<int32_t>(square / static_cast<uint16_t>(Architecture::QuantizationA));
        }
        else
        {
            return clipped;
        }
    }

    static_assert(Architecture::Activation != Architecture::ActivationKind::SquaredClippedReLU ||
        Architecture::QuantizationA <= 255,
        "Activate narrows the squared value through uint16_t, so QuantizationA^2 must fit it");

    // Largest magnitude a single ActivatedDotProduct term can reach: an
    // activated value (bounded by QuantizationA, since Activate always
    // returns a value in [0, QuantizationA]) times the largest magnitude an
    // int16 weight can hold. Derived from the format's own limits rather than
    // assumed, so a future change to QuantizationA is caught here instead of
    // silently reopening the overflow this bound exists to close.
    inline constexpr int64_t MaxActivatedTermMagnitude =
        static_cast<int64_t>(Architecture::QuantizationA) * 32768;

    // Largest number of ActivatedDotProduct terms a kernel may sum in int32
    // before it must widen to Accumulation, for any int16 weight the format
    // permits -- not merely the weights a trained network happens to
    // produce. A kernel accumulates whole vectors at a time, so this only
    // needs to be a safe lower bound, not the tightest one.
    inline constexpr size_t MaxSafeChunkTerms =
        static_cast<size_t>(INT32_MAX / MaxActivatedTermMagnitude);

    // The chunk size ActivatedDotProduct kernels actually use: half of
    // HiddenSize, comfortably inside MaxSafeChunkTerms and a whole multiple
    // of every kernel's vector width.
    inline constexpr size_t ActivationChunk = 256;

    static_assert(ActivationChunk <= MaxSafeChunkTerms,
        "ActivationChunk terms must fit in int32 for any int16 weight the format permits");
    static_assert(Architecture::HiddenSize % ActivationChunk == 0,
        "ActivatedDotProduct's only production caller passes HiddenSize terms");

    // Converts a weighted sum of activations into a centipawn score from the
    // point of view of the side to move.
    constexpr Score Dequantize(Accumulation weightedSum, int32_t outputBias)
    {
        const Accumulation normalized = weightedSum + outputBias;
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
