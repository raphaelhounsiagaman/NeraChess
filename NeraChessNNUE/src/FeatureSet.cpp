#include "FeatureSet.h"

#include "ChessUtil.h"

namespace NeraChessNNUE::FeatureSet
{
    using namespace NeraChessEngine;

    namespace
    {
        // 0 for the perspective's own pieces, 1 for the opponent's.
        constexpr size_t RelativeColour(Perspective perspective, Piece piece)
        {
            const bool pieceIsWhite = piece.IsWhite();
            const bool perspectiveIsWhite = perspective == Perspective::White;
            return pieceIsWhite == perspectiveIsWhite ? 0u : 1u;
        }
    }

    FeatureIndex FeatureIndexOf(Perspective perspective, Piece piece, uint8_t square,
        size_t inputBucket)
    {
        const size_t colour = RelativeColour(perspective, piece);
        const size_t type = static_cast<uint8_t>(piece) % Architecture::PieceTypeCount;
        const size_t relative = RelativeSquare(perspective, square);

        const size_t index =
            (colour * Architecture::PieceTypeCount + type) * Architecture::SquareCount + relative;
        return static_cast<FeatureIndex>(inputBucket * Architecture::PerspectiveInputSize + index);
    }

    uint8_t KingSquare(const BoardState& state, Perspective perspective)
    {
        const Bitboard king = perspective == Perspective::White
            ? state.pieceBitboards[PieceType::WHITE_KING]
            : state.pieceBitboards[PieceType::BLACK_KING];
        return king ? BitUtil::GetLSBIndex(king) : NoSquare;
    }

    void CollectActiveFeatures(const BoardState& state, Perspective perspective,
        ActiveFeatures& out)
    {
        out.Clear();

        const uint8_t kingSquare = KingSquare(state, perspective);
        const size_t bucket = kingSquare == NoSquare ? 0u : KingBucket(perspective, kingSquare);

        for (uint8_t piece = 0; piece < state.pieceBitboards.size(); ++piece)
        {
            Bitboard pieces = state.pieceBitboards[piece];
            while (pieces)
            {
                const uint8_t square = BitUtil::PopLSB(pieces);
                out.Add(FeatureIndexOf(perspective, Piece(piece), square, bucket));
            }
        }
    }

    void ComputeDelta(const DirtyPieces& dirty, Perspective perspective, size_t inputBucket,
        FeatureDelta& out)
    {
        out.Clear();

        for (size_t index = 0; index < dirty.count; ++index)
        {
            const DirtyPiece& piece = dirty.pieces[index];
            if (piece.piece == PieceType::NO_PIECE)
                continue;

            if (piece.from != NoSquare)
            {
                out.removed[out.removedCount++] =
                    FeatureIndexOf(perspective, piece.piece, piece.from, inputBucket);
            }
            if (piece.to != NoSquare)
            {
                out.added[out.addedCount++] =
                    FeatureIndexOf(perspective, piece.piece, piece.to, inputBucket);
            }
        }
    }
}
