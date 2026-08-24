#pragma once

#include "NnueCommon.h"
#include "Quantization.h"

#include <cstddef>
#include <cstring>

// Vector primitives used by the feature transformer and the output layer.
//
// Every kernel here must produce results identical to the scalar reference in
// Simd::Scalar, bit for bit. NNUE inference has to be deterministic across
// machines: two Lazy SMP workers searching the same position must agree, or
// they poison the shared transposition table with contradictory scores. The
// kernels are written so that no intermediate value can overflow for any
// int16 parameters the format permits -- not merely for the small weights a
// trained network happens to produce -- and TestNnueSimdKernels checks the
// vector and scalar paths against each other on adversarial inputs.
//
// Selection is mostly at compile time. The defaults are the widest instruction
// set each platform guarantees: NEON on AArch64, SSE2 on x86-64. AVX2 is used
// throughout when the build enables it (-mavx2, /arch:AVX2), which is worth
// doing for a machine you control but cannot be the default without dropping
// pre-2013 CPUs. The one exception is ActivatedDotProduct on an SSE2-baseline
// GCC/Clang x86 build: it has no SSE2 vector implementation (see below), so it
// instead picks an AVX2 or SSE4.1 clone of the scalar loop at runtime via
// function multiversioning. That choice is resolved once per process and
// every clone is bit-exact with Simd::Scalar by construction, so two Lazy SMP
// workers in the same process -- the only place a disagreement could poison
// the shared transposition table -- always agree.

// AArch64 rather than any NEON: the kernels use vmull_high_s16 and
// vmovl_high_s32, which 32-bit ARM's NEON does not provide.
#if defined(__aarch64__) || defined(_M_ARM64)
    #define NNUE_SIMD_NEON 1
    #include <arm_neon.h>
#elif defined(__AVX2__)
    #define NNUE_SIMD_AVX2 1
    #include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define NNUE_SIMD_SSE2 1
    #include <emmintrin.h>
#endif

namespace NeraChessNNUE::Simd
{
    // -- Scalar reference --------------------------------------------------
    //
    // Always compiled, and the definition of correct. Every vector kernel is
    // tested against these.

    namespace Scalar
    {
        inline void Add(Weight* accumulator, const Weight* column, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                accumulator[index] = static_cast<Weight>(accumulator[index] + column[index]);
        }

        inline void Subtract(Weight* accumulator, const Weight* column, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                accumulator[index] = static_cast<Weight>(accumulator[index] - column[index]);
        }

        inline void AddSubtract(Weight* accumulator, const Weight* added, const Weight* removed,
            size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                accumulator[index] = static_cast<Weight>(
                    accumulator[index] + added[index] - removed[index]);
            }
        }

        // destination[i] = source[i] + column[i]
        //
        // Same arithmetic as Add, but reads the running total from a separate
        // source buffer instead of accumulating in place -- the first delta
        // operation of a Push can then read the parent accumulator and write
        // the child directly, instead of copying the parent over first.
        inline void CopyAdd(Weight* destination, const Weight* source, const Weight* column,
            size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                destination[index] = static_cast<Weight>(source[index] + column[index]);
        }

        // destination[i] = source[i] - column[i]
        inline void CopySubtract(Weight* destination, const Weight* source, const Weight* column,
            size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                destination[index] = static_cast<Weight>(source[index] - column[index]);
        }

        // destination[i] = source[i] + added[i] - removed[i]
        inline void CopyAddSubtract(Weight* destination, const Weight* source,
            const Weight* added, const Weight* removed, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                destination[index] = static_cast<Weight>(
                    source[index] + added[index] - removed[index]);
            }
        }

        inline Accumulation ActivatedDotProduct(const Weight* values, const Weight* weights,
            size_t count)
        {
            Accumulation sum = 0;
            for (size_t index = 0; index < count; ++index)
                sum += Quantization::Activate(values[index]) * weights[index];
            return sum;
        }
    }

    // -- Dispatch ----------------------------------------------------------

#if defined(NNUE_SIMD_NEON)
    inline constexpr size_t Lanes = 8;
    inline const char* TargetName() { return "neon"; }
#elif defined(NNUE_SIMD_AVX2)
    inline constexpr size_t Lanes = 16;
    inline const char* TargetName() { return "avx2"; }
#elif defined(NNUE_SIMD_SSE2)
    inline constexpr size_t Lanes = 8;

    // SSE2 has no vector implementation of ActivatedDotProduct below (SSE2
    // lacks a 32-bit packed multiply, and emulating one costs more than it
    // saves: 248 ns vs 259 ns for a 512-wide call, measured). GCC/Clang
    // function multiversioning lets the binary carry AVX2 and SSE4.1 clones
    // of the scalar loop and pick between them at runtime with
    // __builtin_cpu_supports, without forcing the whole translation unit --
    // and therefore every pre-2013 x86-64 target -- onto a newer baseline.
    // Accumulator kernels (Add/Subtract/AddSubtract) already have an SSE2
    // vector path and are unaffected.
#if defined(__GNUC__) || defined(__clang__)
    #define NNUE_SIMD_X86_DISPATCH 1
#endif

#if defined(NNUE_SIMD_X86_DISPATCH)
    namespace Dispatch
    {
        // Each tier below is bit-exact with Scalar::ActivatedDotProduct by
        // construction: it is the identical loop, recompiled at a wider
        // target so the vectorizer reassociates the additions. That is safe
        // because no reassociation can overflow -- a squared clip times an
        // int16 weight fits in int32, and the running int64 total cannot
        // overflow for any int16 weight the format permits (at most 512
        // terms, each under 65025 * 32767 < 2^31) -- so every grouping of the
        // sum produces the same int64 value.
        __attribute__((target("avx2")))
        inline Accumulation DotAvx2(const Weight* values, const Weight* weights, size_t count)
        {
            Accumulation sum = 0;
            for (size_t index = 0; index < count; ++index)
                sum += Quantization::Activate(values[index]) * weights[index];
            return sum;
        }

        __attribute__((target("sse4.1")))
        inline Accumulation DotSse41(const Weight* values, const Weight* weights, size_t count)
        {
            Accumulation sum = 0;
            for (size_t index = 0; index < count; ++index)
                sum += Quantization::Activate(values[index]) * weights[index];
            return sum;
        }

        // Add/Subtract/AddSubtract and the Copy* variants below already have a
        // native SSE2 vector path (unlike ActivatedDotProduct), so there is no
        // SSE4.1 tier for them: SSE2 already vectorizes this arithmetic and
        // SSE4.1 adds nothing an AVX2 clone doesn't already give. Each clone
        // here is the identical scalar loop recompiled at a wider target;
        // int16 addition/subtraction wraps the same way regardless of lane
        // width, so every clone is bit-exact with Simd::Scalar by construction
        // -- there is no reassociation risk the way there is for the dot
        // product's reduction.
        __attribute__((target("avx2")))
        inline void AddAvx2(Weight* accumulator, const Weight* column, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                accumulator[index] = static_cast<Weight>(accumulator[index] + column[index]);
        }

        __attribute__((target("avx2")))
        inline void SubtractAvx2(Weight* accumulator, const Weight* column, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                accumulator[index] = static_cast<Weight>(accumulator[index] - column[index]);
        }

        __attribute__((target("avx2")))
        inline void AddSubtractAvx2(Weight* accumulator, const Weight* added,
            const Weight* removed, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                accumulator[index] = static_cast<Weight>(
                    accumulator[index] + added[index] - removed[index]);
            }
        }

        __attribute__((target("avx2")))
        inline void CopyAddAvx2(Weight* destination, const Weight* source, const Weight* column,
            size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                destination[index] = static_cast<Weight>(source[index] + column[index]);
        }

        __attribute__((target("avx2")))
        inline void CopySubtractAvx2(Weight* destination, const Weight* source,
            const Weight* column, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
                destination[index] = static_cast<Weight>(source[index] - column[index]);
        }

        __attribute__((target("avx2")))
        inline void CopyAddSubtractAvx2(Weight* destination, const Weight* source,
            const Weight* added, const Weight* removed, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                destination[index] = static_cast<Weight>(
                    source[index] + added[index] - removed[index]);
            }
        }

        enum class Tier { Avx2, Sse41, Baseline };

        inline Tier DetectTier()
        {
            __builtin_cpu_init();
            // __builtin_cpu_supports checks OS-enabled YMM/XMM state (via
            // XGETBV), not just the CPUID feature bit, so it cannot select a
            // tier the OS has not enabled register state for.
            if (__builtin_cpu_supports("avx2"))
                return Tier::Avx2;
            if (__builtin_cpu_supports("sse4.1"))
                return Tier::Sse41;
            return Tier::Baseline;
        }

        // Resolved once; the microarchitecture a process runs on cannot
        // change while it runs.
        inline Tier SelectedTier()
        {
            static const Tier tier = DetectTier();
            return tier;
        }
    }
#endif

    inline const char* TargetName()
    {
#if defined(NNUE_SIMD_X86_DISPATCH)
        switch (Dispatch::SelectedTier())
        {
        case Dispatch::Tier::Avx2:      return "sse2 (dispatch: avx2)";
        case Dispatch::Tier::Sse41:     return "sse2 (dispatch: sse4.1)";
        case Dispatch::Tier::Baseline:  break;
        }
#endif
        return "sse2";
    }
#else
    inline constexpr size_t Lanes = 1;
    inline const char* TargetName() { return "scalar"; }
#endif

    // Architecture::HiddenSize is asserted to be a multiple of 16, so every
    // kernel below runs whole vectors with no tail to handle. The tail loops
    // are kept anyway so the primitives stay usable at any length.

    // accumulator[i] += column[i]
    inline void Add(Weight* accumulator, const Weight* column, size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            vst1q_s16(accumulator + index,
                vaddq_s16(vld1q_s16(accumulator + index), vld1q_s16(column + index)));
        }
        Scalar::Add(accumulator + index, column + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i sum = _mm256_add_epi16(
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator + index)),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(column + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator + index), sum);
        }
        Scalar::Add(accumulator + index, column + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::AddAvx2(accumulator, column, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i sum = _mm_add_epi16(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(accumulator + index)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(column + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(accumulator + index), sum);
        }
        Scalar::Add(accumulator + index, column + index, count - index);
#else
        Scalar::Add(accumulator, column, count);
#endif
    }

    // accumulator[i] -= column[i]
    inline void Subtract(Weight* accumulator, const Weight* column, size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            vst1q_s16(accumulator + index,
                vsubq_s16(vld1q_s16(accumulator + index), vld1q_s16(column + index)));
        }
        Scalar::Subtract(accumulator + index, column + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i difference = _mm256_sub_epi16(
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator + index)),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(column + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator + index), difference);
        }
        Scalar::Subtract(accumulator + index, column + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::SubtractAvx2(accumulator, column, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i difference = _mm_sub_epi16(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(accumulator + index)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(column + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(accumulator + index), difference);
        }
        Scalar::Subtract(accumulator + index, column + index, count - index);
#else
        Scalar::Subtract(accumulator, column, count);
#endif
    }

    // accumulator[i] += added[i] - removed[i]
    //
    // Fused because the common case -- a quiet move -- adds one column and
    // removes one, and doing both in a single pass halves the accumulator
    // traffic. int16 addition wraps identically however the terms are grouped,
    // so this matches the scalar order even when it overflows.
    inline void AddSubtract(Weight* accumulator, const Weight* added, const Weight* removed,
        size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const int16x8_t updated = vsubq_s16(
                vaddq_s16(vld1q_s16(accumulator + index), vld1q_s16(added + index)),
                vld1q_s16(removed + index));
            vst1q_s16(accumulator + index, updated);
        }
        Scalar::AddSubtract(accumulator + index, added + index, removed + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i updated = _mm256_sub_epi16(
                _mm256_add_epi16(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator + index)),
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(added + index))),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(removed + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator + index), updated);
        }
        Scalar::AddSubtract(accumulator + index, added + index, removed + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::AddSubtractAvx2(accumulator, added, removed, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i updated = _mm_sub_epi16(
                _mm_add_epi16(
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(accumulator + index)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(added + index))),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(removed + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(accumulator + index), updated);
        }
        Scalar::AddSubtract(accumulator + index, added + index, removed + index, count - index);
#else
        Scalar::AddSubtract(accumulator, added, removed, count);
#endif
    }

    inline void Copy(Weight* destination, const Weight* source, size_t count)
    {
        std::memcpy(destination, source, count * sizeof(Weight));
    }

    // destination[i] = source[i] + column[i]
    //
    // Same shape as Add, but writes a separate destination instead of
    // accumulating in place -- see Scalar::CopyAdd.
    inline void CopyAdd(Weight* destination, const Weight* source, const Weight* column,
        size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            vst1q_s16(destination + index,
                vaddq_s16(vld1q_s16(source + index), vld1q_s16(column + index)));
        }
        Scalar::CopyAdd(destination + index, source + index, column + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i sum = _mm256_add_epi16(
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source + index)),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(column + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + index), sum);
        }
        Scalar::CopyAdd(destination + index, source + index, column + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::CopyAddAvx2(destination, source, column, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i sum = _mm_add_epi16(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(column + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index), sum);
        }
        Scalar::CopyAdd(destination + index, source + index, column + index, count - index);
#else
        Scalar::CopyAdd(destination, source, column, count);
#endif
    }

    // destination[i] = source[i] - column[i]
    inline void CopySubtract(Weight* destination, const Weight* source, const Weight* column,
        size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            vst1q_s16(destination + index,
                vsubq_s16(vld1q_s16(source + index), vld1q_s16(column + index)));
        }
        Scalar::CopySubtract(destination + index, source + index, column + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i difference = _mm256_sub_epi16(
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source + index)),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(column + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + index), difference);
        }
        Scalar::CopySubtract(destination + index, source + index, column + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::CopySubtractAvx2(destination, source, column, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i difference = _mm_sub_epi16(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(column + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index), difference);
        }
        Scalar::CopySubtract(destination + index, source + index, column + index, count - index);
#else
        Scalar::CopySubtract(destination, source, column, count);
#endif
    }

    // destination[i] = source[i] + added[i] - removed[i]
    //
    // The fused replacement for AccumulatorStack::Push's memcpy-then-delta:
    // reads the parent accumulator once and writes the child directly,
    // instead of copying the parent over and then rewriting it in place.
    inline void CopyAddSubtract(Weight* destination, const Weight* source, const Weight* added,
        const Weight* removed, size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const int16x8_t updated = vsubq_s16(
                vaddq_s16(vld1q_s16(source + index), vld1q_s16(added + index)),
                vld1q_s16(removed + index));
            vst1q_s16(destination + index, updated);
        }
        Scalar::CopyAddSubtract(destination + index, source + index, added + index,
            removed + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i updated = _mm256_sub_epi16(
                _mm256_add_epi16(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source + index)),
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(added + index))),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(removed + index)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + index), updated);
        }
        Scalar::CopyAddSubtract(destination + index, source + index, added + index,
            removed + index, count - index);
#elif defined(NNUE_SIMD_SSE2)
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (Dispatch::SelectedTier() == Dispatch::Tier::Avx2)
        {
            Dispatch::CopyAddSubtractAvx2(destination, source, added, removed, count);
            return;
        }
#endif
        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const __m128i updated = _mm_sub_epi16(
                _mm_add_epi16(
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(added + index))),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(removed + index)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index), updated);
        }
        Scalar::CopyAddSubtract(destination + index, source + index, added + index,
            removed + index, count - index);
#else
        Scalar::CopyAddSubtract(destination, source, added, removed, count);
#endif
    }

    // Sum of Activate(values[i]) * weights[i].
    //
    // With squared clipped ReLU each term is clamp(v, 0, 255)^2 * w. The square
    // is at most 65025 and the weight at most 32767 in magnitude, so a single
    // term always fits in int32 -- but a sum of them does not, which is why the
    // running total is widened to int64 rather than accumulated in int32 and
    // widened at the end.
    inline Accumulation ActivatedDotProduct(const Weight* values, const Weight* weights,
        size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        static_assert(Architecture::Activation ==
            Architecture::ActivationKind::SquaredClippedReLU,
            "the NEON kernel implements squared clipped ReLU");

        const int16x8_t zero = vdupq_n_s16(0);
        const int16x8_t ceiling = vdupq_n_s16(
            static_cast<int16_t>(Architecture::QuantizationA));
        int64x2_t total0 = vdupq_n_s64(0);
        int64x2_t total1 = vdupq_n_s64(0);

        size_t index = 0;
        for (; index + 8 <= count; index += 8)
        {
            const int16x8_t clipped = vminq_s16(
                vmaxq_s16(vld1q_s16(values + index), zero), ceiling);
            const int16x8_t weight = vld1q_s16(weights + index);

            // Widening multiplies: int16 x int16 -> int32, exact by construction.
            const int32x4_t squareLow = vmull_s16(vget_low_s16(clipped), vget_low_s16(clipped));
            const int32x4_t squareHigh = vmull_high_s16(clipped, clipped);
            const int32x4_t weightLow = vmovl_s16(vget_low_s16(weight));
            const int32x4_t weightHigh = vmovl_high_s16(weight);

            // Each product fits in int32; widen before summing so the running
            // total cannot overflow.
            const int32x4_t productLow = vmulq_s32(squareLow, weightLow);
            const int32x4_t productHigh = vmulq_s32(squareHigh, weightHigh);

            total0 = vaddq_s64(total0, vmovl_s32(vget_low_s32(productLow)));
            total1 = vaddq_s64(total1, vmovl_high_s32(productLow));
            total0 = vaddq_s64(total0, vmovl_s32(vget_low_s32(productHigh)));
            total1 = vaddq_s64(total1, vmovl_high_s32(productHigh));
        }

        const int64x2_t combined = vaddq_s64(total0, total1);
        Accumulation sum = vgetq_lane_s64(combined, 0) + vgetq_lane_s64(combined, 1);
        return sum + Scalar::ActivatedDotProduct(values + index, weights + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        static_assert(Architecture::Activation ==
            Architecture::ActivationKind::SquaredClippedReLU,
            "the AVX2 kernel implements squared clipped ReLU");

        const __m256i zero = _mm256_setzero_si256();
        const __m256i ceiling = _mm256_set1_epi16(
            static_cast<short>(Architecture::QuantizationA));
        __m256i total = _mm256_setzero_si256();

        size_t index = 0;
        for (; index + 16 <= count; index += 16)
        {
            const __m256i clipped = _mm256_min_epi16(
                _mm256_max_epi16(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(values + index)), zero),
                ceiling);
            const __m256i weight =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights + index));

            // Widen to int32 in two halves; the square and the product both fit.
            for (int half = 0; half < 2; ++half)
            {
                const __m128i clippedHalf = half == 0
                    ? _mm256_castsi256_si128(clipped)
                    : _mm256_extracti128_si256(clipped, 1);
                const __m128i weightHalf = half == 0
                    ? _mm256_castsi256_si128(weight)
                    : _mm256_extracti128_si256(weight, 1);

                const __m256i wide = _mm256_cvtepi16_epi32(clippedHalf);
                const __m256i wideWeight = _mm256_cvtepi16_epi32(weightHalf);
                const __m256i square = _mm256_mullo_epi32(wide, wide);
                const __m256i product = _mm256_mullo_epi32(square, wideWeight);

                total = _mm256_add_epi64(total,
                    _mm256_cvtepi32_epi64(_mm256_castsi256_si128(product)));
                total = _mm256_add_epi64(total,
                    _mm256_cvtepi32_epi64(_mm256_extracti128_si256(product, 1)));
            }
        }

        alignas(32) int64_t lanes[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), total);
        const Accumulation sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
        return sum + Scalar::ActivatedDotProduct(values + index, weights + index, count - index);
#else
#if defined(NNUE_SIMD_X86_DISPATCH)
        switch (Dispatch::SelectedTier())
        {
        case Dispatch::Tier::Avx2:
            return Dispatch::DotAvx2(values, weights, count);
        case Dispatch::Tier::Sse41:
            return Dispatch::DotSse41(values, weights, count);
        case Dispatch::Tier::Baseline:
            break;
        }
#endif
        // SSE2 lacks a 32-bit packed multiply (that is SSE4.1), and emulating
        // one costs more than it saves, so hardware the dispatch tiers above
        // reject (or a non-GCC/Clang toolchain) falls back to scalar.
        return Scalar::ActivatedDotProduct(values, weights, count);
#endif
    }
}
