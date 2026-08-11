#pragma once

#include <cstddef>
#include <cstdint>

// Shared vocabulary for the NNUE inference library.
//
// NNUE ("efficiently updatable neural network") evaluates a position from the
// point of view of both players. Every quantity in this library is therefore
// expressed relative to a perspective rather than to White, and the two
// perspectives are combined only in the output layer.

// Debug builds re-derive every incrementally updated accumulator from scratch
// and assert that the two agree. This is the check that catches a wrong
// dirty-piece list; without it a bad delta shows up only as an engine that
// evaluates subtly wrong positions and plays slightly worse, with nothing
// pointing at the cause. It costs a full refresh per evaluation, so Release
// builds leave it out.
#if !defined(NNUE_VERIFY_ACCUMULATOR)
    #if defined(DEBUG) && !defined(NDEBUG)
        #define NNUE_VERIFY_ACCUMULATOR 1
    #else
        #define NNUE_VERIFY_ACCUMULATOR 0
    #endif
#endif

namespace NeraChessNNUE
{
    // Half-precision accumulator storage. The feature transformer sums one
    // int16_t column per active feature, so this is the widest type that keeps
    // the accumulator cache friendly.
    using Weight = int16_t;
    using Bias = int16_t;

    // Wide accumulator used inside the output layer. Squared clipped ReLU
    // multiplies two quantized values before weighting them, so the reference
    // implementation accumulates in 64 bits and never has to reason about
    // overflow. SIMD kernels may narrow this once they carry their own bounds
    // argument.
    using Accumulation = int64_t;

    // A centipawn-scale evaluation from the side-to-move point of view, using
    // the same convention as NeraChessSearch::Score.
    using Score = int32_t;

    // Index of a single active input feature within one perspective.
    using FeatureIndex = uint16_t;

    enum class Perspective : uint8_t
    {
        White = 0,
        Black = 1,
    };

    inline constexpr size_t PerspectiveCount = 2;

    constexpr Perspective Opposite(Perspective perspective)
    {
        return perspective == Perspective::White ? Perspective::Black : Perspective::White;
    }

    constexpr size_t Index(Perspective perspective)
    {
        return static_cast<size_t>(perspective);
    }
}
