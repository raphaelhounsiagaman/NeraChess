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

    // Canonical square of a perspective's own king: the square the view's own
    // numbering puts it on. Mirroring means this is always on files e..h, so
    // only 32 of the 64 squares can ever come out of it.
    constexpr uint8_t CanonicalKingSquare(Perspective perspective, uint8_t kingSquare)
    {
        const uint8_t relative = RelativeSquare(perspective, kingSquare);
        return OrientationOfKing(relative) == Orientation::Mirrored ? MirroredSquare(relative)
                                                                   : relative;
    }

    // Feature-transformer weight matrix each canonical king square selects,
    // indexed by CanonicalKingBucketSlot below. Rows run from the king's own
    // first rank upwards; columns are canonical files e, f, g, h.
    //
    //             file:  e  f  g  h
    //    relative rank 1:  0  0  1  1
    //    relative rank 2:  2  2  3  3
    //    relative rank 3:  4  4  5  5
    //    relative rank 4:  4  4  5  5
    //    relative rank 5:  6  6  7  7
    //    relative rank 6:  6  6  7  7
    //    relative rank 7:  6  6  7  7
    //    relative rank 8:  6  6  7  7
    //
    // Two things shape this. Buckets are spent where the positions are: a king
    // on its own first two ranks is the overwhelmingly common case, so those
    // get four of the eight. And adjacent files are paired so that the king
    // shuffles that happen most -- g1<->h1, e1<->f1 -- stay inside a bucket
    // and cost no refresh. Kg1-g2 and castling e1->g1 do cross, deliberately:
    // those are real changes of king safety, which is what buckets are for.
    inline constexpr std::array<uint8_t, 32> KingBucketTable = { {
        0, 0, 1, 1, // rank 1
        2, 2, 3, 3, // rank 2
        4, 4, 5, 5, // rank 3
        4, 4, 5, 5, // rank 4
        6, 6, 7, 7, // rank 5
        6, 6, 7, 7, // rank 6
        6, 6, 7, 7, // rank 7
        6, 6, 7, 7, // rank 8
    } };

    // Where a canonical king square sits in KingBucketTable. Canonical files
    // are e..h, so subtracting 4 packs the four of them into a row.
    constexpr size_t CanonicalKingBucketSlot(uint8_t canonicalKingSquare)
    {
        const size_t rank = canonicalKingSquare / 8u;
        const size_t file = canonicalKingSquare % 8u;
        return rank * 4u + (file - 4u);
    }

    // Feature-transformer weight matrix selected by the perspective's own king.
    //
    // Takes the canonical square, not the raw one. Deriving the bucket from
    // the raw square would put a position and its horizontal reflection in
    // different buckets, which is exactly what the mirroring above exists to
    // prevent.
    constexpr size_t KingBucket(uint8_t canonicalKingSquare)
    {
        return KingBucketTable[CanonicalKingBucketSlot(canonicalKingSquare)];
    }

    // The table and Architecture::InputBucketCount are two statements of the
    // same fact, and a network's size depends on the constant while its
    // meaning depends on the table. Checking that every bucket is both in
    // range and actually used is what stops them drifting apart: a layout that
    // leaves a bucket empty would silently ship weights nothing can reach.
    namespace Detail
    {
        constexpr bool EveryBucketIsUsed()
        {
            std::array<bool, Architecture::InputBucketCount> seen{};
            for (const uint8_t bucket : KingBucketTable)
            {
                if (bucket >= Architecture::InputBucketCount)
                    return false;
                seen[bucket] = true;
            }
            for (const bool used : seen)
            {
                if (!used)
                    return false;
            }
            return true;
        }
    }

    static_assert(Detail::EveryBucketIsUsed(),
        "KingBucketTable must map into [0, InputBucketCount) and use every bucket");

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
            static_cast<uint8_t>(KingBucket(CanonicalKingSquare(perspective, kingSquare))) };
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
    // accumulator and forces a full refresh of it. True when the king crosses
    // the d/e boundary, and when it changes input bucket.
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
