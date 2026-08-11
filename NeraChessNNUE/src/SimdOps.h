#pragma once

#include "NnueCommon.h"
#include "Quantization.h"

#include <cstddef>

// Vector primitives used by the feature transformer and the output layer.
//
// Only the portable scalar path exists today. It is the reference the SIMD
// kernels must reproduce bit for bit: NNUE inference has to be deterministic
// across machines, otherwise two workers searching the same position disagree
// and the transposition table becomes inconsistent.
//
// TODO(nnue): add AVX2, SSE4.1, and NEON kernels behind NNUE_SIMD_* once the
// scalar path is correct and there is a network to measure against. The
// accumulator update is memory bound and the output layer is the only place
// where widening matters, so those two functions are the whole surface.

#if defined(__AVX2__)
    #define NNUE_SIMD_TARGET "avx2"
#elif defined(__SSE4_1__)
    #define NNUE_SIMD_TARGET "sse4.1"
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    #define NNUE_SIMD_TARGET "neon"
#else
    #define NNUE_SIMD_TARGET "scalar"
#endif

namespace NeraChessNNUE::Simd
{
    // Name of the kernel family that would be selected on this build. Reported
    // by the UCI "eval" command so a slow build is visible instead of silent.
    // Reads "scalar (reference)" until the vector kernels land.
    inline constexpr const char* TargetName = NNUE_SIMD_TARGET " (reference)";

    // accumulator[i] += column[i]
    inline void Add(Weight* accumulator, const Weight* column, size_t count)
    {
        for (size_t index = 0; index < count; ++index)
            accumulator[index] = static_cast<Weight>(accumulator[index] + column[index]);
    }

    // accumulator[i] -= column[i]
    inline void Subtract(Weight* accumulator, const Weight* column, size_t count)
    {
        for (size_t index = 0; index < count; ++index)
            accumulator[index] = static_cast<Weight>(accumulator[index] - column[index]);
    }

    // accumulator[i] += added[i] - removed[i]
    //
    // Fused because the common case -- a quiet move -- adds one column and
    // removes one, and doing both in a single pass halves the accumulator
    // traffic.
    inline void AddSubtract(Weight* accumulator, const Weight* added, const Weight* removed,
        size_t count)
    {
        for (size_t index = 0; index < count; ++index)
        {
            accumulator[index] = static_cast<Weight>(
                accumulator[index] + added[index] - removed[index]);
        }
    }

    inline void Copy(Weight* destination, const Weight* source, size_t count)
    {
        for (size_t index = 0; index < count; ++index)
            destination[index] = source[index];
    }

    // Sum of Activate(values[i]) * weights[i].
    inline Accumulation ActivatedDotProduct(const Weight* values, const Weight* weights,
        size_t count)
    {
        Accumulation sum = 0;
        for (size_t index = 0; index < count; ++index)
            sum += Quantization::Activate(values[index]) * weights[index];
        return sum;
    }
}
