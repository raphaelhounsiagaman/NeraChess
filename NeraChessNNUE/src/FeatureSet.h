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

    // Horizontal reflection of a square: a <-> h, b <-> g, c <-> f, d <-> e.
    // Square::a1 is 0 and Square::h1 is 7, so the low three bits hold the file
    // and flipping them reflects the board about the d/e boundary.
    constexpr uint8_t MirroredSquare(uint8_t square)
    {
        return static_cast<uint8_t>(square ^ 7);
    }

    // Which way round a perspective reads the board horizontally.
    //
    // Features are canonicalized so that a perspective's own king always
    // stands on files e..h. A king on files a..d reflects every square of that
    // perspective, which lets one set of weights answer for a pattern and its
    // horizontal mirror instead of having to learn both.
    enum class Orientation : uint8_t
    {
        Direct = 0,   // the own king already stands on files e..h
        Mirrored = 1, // the own king stands on files a..d; squares reflect
    };

    // Orientation a king square implies.
    //
    // Takes the perspective-relative square, which is what features are
    // expressed in. The vertical flip preserves the file, so reading the file
    // off the absolute square would give the same answer.
    constexpr Orientation OrientationOfKing(uint8_t relativeKingSquare)
    {
        // Files a, b, c, d (0..3) mirror; e, f, g, h (4..7) stay put.
        return (relativeKingSquare & 7u) < 4u ? Orientation::Mirrored : Orientation::Direct;
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

    // Everything that decides how one perspective's features are numbered:
    // which weight matrix they index, and which way round the board is read.
    //
    // Both follow the perspective's own king, so the two perspectives of a
    // position can and regularly do disagree. Deriving a view once and passing
    // it around is what keeps king-file reasoning out of the rest of the NNUE
    // code, and what lets the accumulator notice that a half it already holds
    // was built under a view that no longer applies.
    struct View
    {
        Perspective perspective = Perspective::White;
        Orientation orientation = Orientation::Direct;
        uint8_t inputBucket = 0;

        bool operator==(const View&) const = default;
    };

    // View for a perspective whose own king stands on the given square.
    constexpr View ViewOfKing(Perspective perspective, uint8_t kingSquare)
    {
        return View{ perspective, OrientationOfKing(RelativeSquare(perspective, kingSquare)),
            static_cast<uint8_t>(KingBucket(perspective, kingSquare)) };
    }

    // View a perspective's half of the accumulator must be built with. A
    // position holding no king for that side -- only malformed test positions
    // do -- reads Direct, and the Python indexer makes the same choice.
    View ViewOf(const NeraChessEngine::BoardState& state, Perspective perspective);

    // Square as a view numbers it: the perspective's vertical flip first, then
    // the horizontal reflection when the view is mirrored. The two operate on
    // disjoint bits, so their order is a matter of exposition only.
    constexpr uint8_t OrientedSquare(const View& view, uint8_t square)
    {
        const uint8_t relative = RelativeSquare(view.perspective, square);
        return view.orientation == Orientation::Mirrored ? MirroredSquare(relative) : relative;
    }

    // Whether moving the perspective's own king from one square to another
    // renumbers that perspective's features, which invalidates its half of the
    // accumulator and forces a full refresh of it. True exactly when the king
    // crosses the d/e boundary, or changes input bucket once buckets exist.
    constexpr bool RequiresRefresh(Perspective perspective, uint8_t fromKingSquare,
        uint8_t toKingSquare)
    {
        return ViewOfKing(perspective, fromKingSquare) != ViewOfKing(perspective, toKingSquare);
    }

    // Index of the (piece, square) feature within a perspective's input space,
    // including the input-bucket offset.
    //
    // Layout: ((relativeColour * PieceTypeCount + pieceType) * SquareCount +
    //          orientedSquare) + bucket * PerspectiveInputSize
    //
    // relativeColour is 0 for the perspective's own pieces and 1 for the
    // opponent's, so a position and its colour-flipped mirror produce mirrored
    // feature sets and can share one weight matrix.
    FeatureIndex FeatureIndexOf(const View& view, NeraChessEngine::Piece piece, uint8_t square);

    // Every feature active for one perspective in the given position.
    void CollectActiveFeatures(const NeraChessEngine::BoardState& state,
        Perspective perspective, ActiveFeatures& out);

    // Square of the perspective's own king, or NoSquare when the board has no
    // such king (which only happens in malformed test positions).
    uint8_t KingSquare(const NeraChessEngine::BoardState& state, Perspective perspective);

    // Translates a dirty-piece list into the features one perspective must add
    // and remove. Only valid while the view is the one the half was built
    // with; a move that changes the view needs a refresh instead.
    void ComputeDelta(const DirtyPieces& dirty, const View& view, FeatureDelta& out);
}
