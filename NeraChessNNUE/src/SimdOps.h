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

        // Activate already rescales its result onto [0, QuantizationA], so
        // each term fits int32 with room to spare; only a run of them needs
        // widening. Summing Quantization::ActivationChunk terms at a time in
        // int32 before folding into the int64 total is safe for any int16
        // weight the format permits (see Quantization::MaxSafeChunkTerms) and
        // -- because plain integer addition is associative and none of these
        // partial sums can overflow -- gives the same total as adding every
        // term into the int64 accumulator directly.
        inline Accumulation ActivatedDotProduct(const Weight* values, const Weight* weights,
            size_t count)
        {
            Accumulation total = 0;
            size_t index = 0;
            for (; index + Quantization::ActivationChunk <= count; index += Quantization::ActivationChunk)
            {
                int32_t chunk = 0;
                for (size_t offset = 0; offset < Quantization::ActivationChunk; ++offset)
                    chunk += Quantization::Activate(values[index + offset]) * weights[index + offset];
                total += chunk;
            }
            int32_t tail = 0;
            for (; index < count; ++index)
                tail += Quantization::Activate(values[index]) * weights[index];
            return total + tail;
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
        // target so the vectorizer reassociates the additions and, for the
        // activation's division, lowers a compile-time-constant unsigned
        // divide into a reciprocal-multiply sequence. That reassociation is
        // safe because no intermediate can overflow -- Activate's result
        // fits int32 with room to spare, a chunk of Quantization::
        // ActivationChunk terms cannot overflow int32 for any int16 weight
        // the format permits, and plain integer addition of exact terms
        // produces the same total regardless of grouping -- so every
        // grouping of the sum produces the same int64 value.
        __attribute__((target("avx2")))
        inline Accumulation DotAvx2(const Weight* values, const Weight* weights, size_t count)
        {
            Accumulation total = 0;
            size_t index = 0;
            for (; index + Quantization::ActivationChunk <= count; index += Quantization::ActivationChunk)
            {
                int32_t chunk = 0;
                for (size_t offset = 0; offset < Quantization::ActivationChunk; ++offset)
                    chunk += Quantization::Activate(values[index + offset]) * weights[index + offset];
                total += chunk;
            }
            int32_t tail = 0;
            for (; index < count; ++index)
                tail += Quantization::Activate(values[index]) * weights[index];
            return total + tail;
        }

        __attribute__((target("sse4.1")))
        inline Accumulation DotSse41(const Weight* values, const Weight* weights, size_t count)
        {
            Accumulation total = 0;
            size_t index = 0;
            for (; index + Quantization::ActivationChunk <= count; index += Quantization::ActivationChunk)
            {
                int32_t chunk = 0;
                for (size_t offset = 0; offset < Quantization::ActivationChunk; ++offset)
                    chunk += Quantization::Activate(values[index + offset]) * weights[index + offset];
                total += chunk;
            }
            int32_t tail = 0;
            for (; index < count; ++index)
                tail += Quantization::Activate(values[index]) * weights[index];
            return total + tail;
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
    // Activate rescales the squared clip back onto [0, QuantizationA] (see
    // Quantization.h), so a term is a single int16 x int16 -> int32 widening
    // multiply-add instead of the int32 x int32 multiply a wider activation
    // would need. A chunk of Quantization::ActivationChunk terms cannot
    // overflow int32 for any int16 weight the format permits, so the running
    // total only needs to widen to int64 once per chunk rather than per term.
    inline Accumulation ActivatedDotProduct(const Weight* values, const Weight* weights,
        size_t count)
    {
#if defined(NNUE_SIMD_NEON)
        static_assert(Architecture::Activation ==
            Architecture::ActivationKind::SquaredClippedReLU,
            "the NEON kernel implements squared clipped ReLU");
        static_assert(Architecture::QuantizationA == 255,
            "the NEON activation divide is the exact bit trick for /255 specifically");

        constexpr size_t VectorWidth = 8;
        constexpr size_t VectorsPerChunk = Quantization::ActivationChunk / VectorWidth;
        static_assert(Quantization::ActivationChunk % VectorWidth == 0,
            "ActivationChunk must be a whole number of NEON vectors");

        const int16x8_t zero = vdupq_n_s16(0);
        const int16x8_t ceiling = vdupq_n_s16(
            static_cast<int16_t>(Architecture::QuantizationA));
        const uint16x8_t one = vdupq_n_u16(1);

        Accumulation total = 0;
        int32x4_t chunkLow = vdupq_n_s32(0);
        int32x4_t chunkHigh = vdupq_n_s32(0);
        size_t vectorsInChunk = 0;

        size_t index = 0;
        for (; index + VectorWidth <= count; index += VectorWidth)
        {
            const int16x8_t clipped = vminq_s16(
                vmaxq_s16(vld1q_s16(values + index), zero), ceiling);
            const int16x8_t weight = vld1q_s16(weights + index);

            // clipped^2 <= 255^2 = 65025 fits the low 16 bits of the packed
            // multiply exactly (no widening needed), so this is the same bit
            // pattern as an unsigned squaring would produce.
            const uint16x8_t square = vreinterpretq_u16_s16(vmulq_s16(clipped, clipped));

            // Exact unsigned division by 255 for any 16-bit value: floor(x / 255)
            // == ((x + 1) + ((x + 1) >> 8)) >> 8, verified exhaustively over
            // [0, 65535]. Logical (not arithmetic) shifts, hence the unsigned type.
            const uint16x8_t biased = vaddq_u16(square, one);
            const uint16x8_t activatedU =
                vshrq_n_u16(vaddq_u16(biased, vshrq_n_u16(biased, 8)), 8);
            const int16x8_t activated = vreinterpretq_s16_u16(activatedU);

            // Widening multiply-accumulate: int16 x int16 -> int32, exact
            // because activated <= QuantizationA and the weight magnitude is
            // bounded to fit int16, so one product cannot overflow int32.
            chunkLow = vmlal_s16(chunkLow, vget_low_s16(activated), vget_low_s16(weight));
            chunkHigh = vmlal_high_s16(chunkHigh, activated, weight);

            if (++vectorsInChunk == VectorsPerChunk)
            {
                total += vaddvq_s32(vaddq_s32(chunkLow, chunkHigh));
                chunkLow = vdupq_n_s32(0);
                chunkHigh = vdupq_n_s32(0);
                vectorsInChunk = 0;
            }
        }

        total += vaddvq_s32(vaddq_s32(chunkLow, chunkHigh));
        return total + Scalar::ActivatedDotProduct(values + index, weights + index, count - index);
#elif defined(NNUE_SIMD_AVX2)
        static_assert(Architecture::Activation ==
            Architecture::ActivationKind::SquaredClippedReLU,
            "the AVX2 kernel implements squared clipped ReLU");
        static_assert(Architecture::QuantizationA == 255,
            "the AVX2 activation divide is the exact bit trick for /255 specifically");

        constexpr size_t VectorWidth = 16;
        constexpr size_t VectorsPerChunk = Quantization::ActivationChunk / VectorWidth;
        static_assert(Quantization::ActivationChunk % VectorWidth == 0,
            "ActivationChunk must be a whole number of AVX2 vectors");

        const __m256i zero = _mm256_setzero_si256();
        const __m256i ceiling = _mm256_set1_epi16(
            static_cast<short>(Architecture::QuantizationA));
        const __m256i one = _mm256_set1_epi16(1);

        const auto horizontalSum = [](__m256i vector) -> Accumulation
        {
            alignas(32) int32_t lanes[8];
            _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), vector);
            Accumulation sum = 0;
            for (const int32_t lane : lanes)
                sum += lane;
            return sum;
        };

        Accumulation total = 0;
        __m256i chunkTotal = _mm256_setzero_si256();
        size_t vectorsInChunk = 0;

        size_t index = 0;
        for (; index + VectorWidth <= count; index += VectorWidth)
        {
            const __m256i clipped = _mm256_min_epi16(
                _mm256_max_epi16(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(values + index)), zero),
                ceiling);
            const __m256i weight =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights + index));

            // clipped^2 <= 255^2 = 65025 fits the low 16 bits of the packed
            // multiply exactly (no widening needed), so this is the same bit
            // pattern as an unsigned squaring would produce.
            const __m256i square = _mm256_mullo_epi16(clipped, clipped);

            // Exact unsigned division by 255 for any 16-bit value: floor(x / 255)
            // == ((x + 1) + ((x + 1) >> 8)) >> 8, verified exhaustively over
            // [0, 65535]. _mm256_srli_epi16 shifts logically, which is what makes
            // this correct even though the biased value can look negative as int16.
            const __m256i biased = _mm256_add_epi16(square, one);
            const __m256i activated =
                _mm256_srli_epi16(_mm256_add_epi16(biased, _mm256_srli_epi16(biased, 8)), 8);

            // Widening multiply-add: sums each adjacent pair of int16 products
            // into one int32 lane, exact because activated <= QuantizationA and
            // the weight magnitude is bounded to fit int16, so neither a single
            // product nor a pair of them can overflow int32.
            chunkTotal = _mm256_add_epi32(chunkTotal, _mm256_madd_epi16(activated, weight));

            if (++vectorsInChunk == VectorsPerChunk)
            {
                total += horizontalSum(chunkTotal);
                chunkTotal = _mm256_setzero_si256();
                vectorsInChunk = 0;
            }
        }

        total += horizontalSum(chunkTotal);
        return total + Scalar::ActivatedDotProduct(values + index, weights + index, count - index);
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
