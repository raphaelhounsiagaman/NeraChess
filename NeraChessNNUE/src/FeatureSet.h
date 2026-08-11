#pragma once

#include "DirtyPieces.h"
#include "NetworkArchitecture.h"
#include "NnueCommon.h"

#include "BoardState.h"
#include "Piece.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Feature indexing for the input layer.
//
// The C++ indexer here and the Python indexer in
// NNUETraining/nnue_training/features.py must agree exactly. Their agreement
// is checked by NNUETraining/tests/test_features.py against the vectors in
// NNUETraining/tests/feature_vectors.json, which are produced by
// NeraChessTests --nnue-feature-vectors.

namespace NeraChessNNUE::FeatureSet
{
    // At most 32 pieces stand on a legal board, so one perspective never has
    // more than 32 active features.
    inline constexpr size_t MaxActiveFeatures = 32;

    struct ActiveFeatures
    {
        std::array<FeatureIndex, MaxActiveFeatures> indices{};
        size_t count = 0;

        void Add(FeatureIndex index)
        {
            if (count >= MaxActiveFeatures)
                return;
            indices[count++] = index;
        }

        void Clear() { count = 0; }
    };

    // Features that a move switches on and off for one perspective.
    struct FeatureDelta
    {
        std::array<FeatureIndex, MaxDirtyPieces> added{};
        size_t addedCount = 0;
        std::array<FeatureIndex, MaxDirtyPieces> removed{};
        size_t removedCount = 0;

        void Clear()
        {
            addedCount = 0;
            removedCount = 0;
        }
    };

    // Square as seen from a perspective. Black's view is the board flipped
    // vertically, so a1 for White and a8 for Black map to the same index.
    constexpr uint8_t RelativeSquare(Perspective perspective, uint8_t square)
    {
        return perspective == Perspective::White ? square : static_cast<uint8_t>(square ^ 56);
    }

    // Feature-transformer weight matrix selected by the perspective's own king.
    // With Architecture::InputBucketCount == 1 this is always 0; the parameter
    // is kept so that king-bucketed feature sets only change this function.
    constexpr size_t KingBucket(Perspective, uint8_t)
    {
        static_assert(Architecture::InputBucketCount == 1,
            "KingBucket must map king squares to buckets once buckets exist");
        return 0;
    }

    // Whether moving the perspective's own king from one square to another
    // changes the input bucket, which would invalidate the accumulator and
    // force a full refresh. Always false while the feature set is king
    // independent.
    constexpr bool RequiresRefresh(Perspective perspective, uint8_t fromKingSquare,
        uint8_t toKingSquare)
    {
        return KingBucket(perspective, fromKingSquare) !=
            KingBucket(perspective, toKingSquare);
    }

    // Index of the (piece, square) feature within a perspective's input space,
    // including the input-bucket offset.
    //
    // Layout: ((relativeColour * PieceTypeCount + pieceType) * SquareCount +
    //          relativeSquare) + bucket * PerspectiveInputSize
    //
    // relativeColour is 0 for the perspective's own pieces and 1 for the
    // opponent's, so a position and its colour-flipped mirror produce mirrored
    // feature sets and can share one weight matrix.
    FeatureIndex FeatureIndexOf(Perspective perspective, NeraChessEngine::Piece piece,
        uint8_t square, size_t inputBucket = 0);

    // Every feature active for one perspective in the given position.
    void CollectActiveFeatures(const NeraChessEngine::BoardState& state,
        Perspective perspective, ActiveFeatures& out);

    // Square of the perspective's own king, or NoSquare when the board has no
    // such king (which only happens in malformed test positions).
    uint8_t KingSquare(const NeraChessEngine::BoardState& state, Perspective perspective);

    // Translates a dirty-piece list into the features one perspective must add
    // and remove.
    void ComputeDelta(const DirtyPieces& dirty, Perspective perspective,
        size_t inputBucket, FeatureDelta& out);
}
