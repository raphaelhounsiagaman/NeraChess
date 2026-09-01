#pragma once

#include "NnueCommon.h"

#include <cstddef>
#include <cstdint>

// Single source of truth for the network shape.
//
// Every other component -- the feature indexer, the accumulator, the file
// format, and the Python trainer in ../../NNUETraining -- derives its sizes
// from these constants. Changing a value here changes ArchitectureHash(),
// which makes previously serialized networks fail to load instead of being
// silently misinterpreted.
//
// The current shape is:
//
//     (768x8 -> 512)x2 -> 1
//
// with eight input buckets and one output bucket. Features are horizontally
// canonicalized on the perspective's own king, and that king's canonical
// square then selects which of the eight feature-transformer matrices its
// features index (see FeatureSetVersion below). Output buckets by material
// count are the remaining intended step; that bucket dimension already exists
// so that adding it does not require reshaping call sites. See docs/NNUE.md.

namespace NeraChessNNUE::Architecture
{
    // -- Input features ----------------------------------------------------

    inline constexpr size_t SquareCount = 64;
    inline constexpr size_t PieceTypeCount = 6;

    // What a feature index means, independent of how many there are.
    //
    //   1  (relative colour, piece type, relative square); king independent
    //   2  adds horizontal mirroring: a perspective's squares are reflected
    //      when its own king stands on files a-d, so that its king is always
    //      seen on files e-h
    //   3  adds king buckets: the canonical square of a perspective's own king
    //      selects which feature-transformer matrix its features index, via
    //      the table in FeatureSet::KingBucketTable
    //
    // Bumped whenever a feature index comes to mean something new while every
    // dimension stays the same. Dimensions are caught by the sizes below; this
    // is what catches a change the sizes cannot see. It is mixed into
    // ArchitectureHash(), so a network trained under an older feature set is
    // rejected at load time instead of being read as though nothing changed.
    //
    // Note that InputBucketCount is a dimension and is caught on its own. This
    // version is what distinguishes two *layouts* with the same bucket count:
    // re-tuning KingBucketTable without changing how many buckets it uses
    // leaves every size field identical, so it must bump this instead.
    inline constexpr uint16_t FeatureSetVersion = 3;

    // Features are (relative colour, piece type, canonical square) triples, so
    // both perspectives share one weight matrix and a position and its mirror
    // produce mirrored feature sets.
    inline constexpr size_t PerspectiveInputSize =
        PerspectiveCount * PieceTypeCount * SquareCount; // 768

    // Number of separate feature-transformer weight matrices selected by the
    // position of the perspective's own king. A king move forces a refresh of
    // its own half whenever it changes that half's bucket, on top of the
    // refresh it already forces by flipping the half's horizontal orientation.
    //
    // Mirroring canonicalizes the own king onto files e-h, so the mapping's
    // domain is 32 squares; FeatureSet::KingBucketTable is what divides them,
    // and this must equal the number of distinct values in that table.
    inline constexpr size_t InputBucketCount = 8;

    inline constexpr size_t TotalInputSize = InputBucketCount * PerspectiveInputSize;

    // -- Hidden layer ------------------------------------------------------

    // Accumulator width per perspective. The output layer sees both
    // perspectives concatenated, so it consumes 2 * HiddenSize values.
    inline constexpr size_t HiddenSize = 512;

    inline constexpr size_t OutputInputSize = PerspectiveCount * HiddenSize;

    // -- Output layer ------------------------------------------------------

    // Number of output heads selected by a position property (piece count is
    // the usual choice). One head means every position uses the same weights.
    inline constexpr size_t OutputBucketCount = 1;

    // -- Quantization ------------------------------------------------------

    // Feature-transformer weights and accumulator values are stored as
    // round(x * QuantizationA); output weights as round(x * QuantizationB).
    // QuantizationA doubles as the clipping ceiling of the activation, so a
    // fully saturated neuron holds exactly QuantizationA.
    inline constexpr int32_t QuantizationA = 255;
    inline constexpr int32_t QuantizationB = 64;

    // Maps the network's [0, 1] win-probability-like output onto centipawns.
    inline constexpr int32_t EvalScale = 400;

    enum class ActivationKind : uint16_t
    {
        ClippedReLU = 0,
        SquaredClippedReLU = 1,
    };

    inline constexpr ActivationKind Activation = ActivationKind::SquaredClippedReLU;

    // -- Derived sizes -----------------------------------------------------

    inline constexpr size_t FeatureWeightCount = TotalInputSize * HiddenSize;
    inline constexpr size_t FeatureBiasCount = HiddenSize;
    inline constexpr size_t OutputWeightCount = OutputBucketCount * OutputInputSize;
    inline constexpr size_t OutputBiasCount = OutputBucketCount;

    inline constexpr size_t TotalParameterCount =
        FeatureWeightCount + FeatureBiasCount + OutputWeightCount + OutputBiasCount;

    inline constexpr size_t TotalParameterBytes = TotalParameterCount * sizeof(Weight);

    // -- Architecture identity --------------------------------------------

    // FNV-1a over the architecture constants. Serialized networks embed this
    // value so that a network trained for a different shape is rejected at
    // load time rather than producing nonsense evaluations.
    constexpr uint32_t ArchitectureHash()
    {
        constexpr uint32_t Prime = 16'777'619u;
        uint32_t hash = 2'166'136'261u;

        const auto mix = [&hash](uint64_t value)
        {
            for (int byte = 0; byte < 8; ++byte)
            {
                hash ^= static_cast<uint32_t>((value >> (byte * 8)) & 0xFFu);
                hash *= Prime;
            }
        };

        mix(FeatureSetVersion);
        mix(PerspectiveInputSize);
        mix(InputBucketCount);
        mix(HiddenSize);
        mix(OutputBucketCount);
        mix(static_cast<uint64_t>(QuantizationA));
        mix(static_cast<uint64_t>(QuantizationB));
        mix(static_cast<uint64_t>(EvalScale));
        mix(static_cast<uint64_t>(Activation));
        return hash;
    }

    // -- Invariants --------------------------------------------------------

    static_assert(PerspectiveInputSize == 768,
        "feature layout changed; regenerate NNUETraining/nnue_training/architecture.py");
    static_assert(HiddenSize % 16 == 0,
        "hidden size must stay a multiple of 16 so SIMD kernels need no tail handling");
    static_assert(InputBucketCount >= 1 && OutputBucketCount >= 1,
        "bucket counts must be positive");
    static_assert(QuantizationA > 0 && QuantizationB > 0 && EvalScale > 0,
        "quantization constants must be positive");
    static_assert(TotalInputSize <= UINT16_MAX,
        "FeatureIndex must be able to address every input feature");
}
