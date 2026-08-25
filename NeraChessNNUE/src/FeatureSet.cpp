#include "FeatureSet.h"

#include "ChessUtil.h"

namespace NeraChessNNUE::FeatureSet
{
    using namespace NeraChessEngine;

    namespace
    {
        // 0 for the perspective's own pieces, 1 for the opponent's.
        //
        // Not constexpr: Piece::IsWhite is a plain member function, so no call
        // to this could ever be constant evaluated. That is ill-formed, and
        // MSVC rejects it outright where Clang and GCC stay quiet.
        inline size_t RelativeColour(Perspective perspective, Piece piece)
        {
            const bool pieceIsWhite = piece.IsWhite();
            const bool perspectiveIsWhite = perspective == Perspective::White;
            return pieceIsWhite == perspectiveIsWhite ? 0u : 1u;
        }
    }

    FeatureIndex FeatureIndexOf(const View& view, Piece piece, uint8_t square)
    {
        const size_t colour = RelativeColour(view.perspective, piece);
        const size_t type = static_cast<uint8_t>(piece) % Architecture::PieceTypeCount;
        const size_t oriented = OrientedSquare(view, square);

        const size_t index =
            (colour * Architecture::PieceTypeCount + type) * Architecture::SquareCount + oriented;
        return static_cast<FeatureIndex>(
            view.inputBucket * Architecture::PerspectiveInputSize + index);
    }

    uint8_t KingSquare(const BoardState& state, Perspective perspective)
    {
        const Bitboard king = perspective == Perspective::White
            ? state.pieceBitboards[PieceType::WHITE_KING]
            : state.pieceBitboards[PieceType::BLACK_KING];
        return king ? BitUtil::GetLSBIndex(king) : NoSquare;
    }

    View ViewOf(const BoardState& state, Perspective perspective)
    {
        const uint8_t kingSquare = KingSquare(state, perspective);
        if (kingSquare == NoSquare)
            return View{ perspective, Orientation::Direct, 0 };

        return ViewOfKing(perspective, kingSquare);
    }

    void CollectActiveFeatures(const BoardState& state, Perspective perspective,
        ActiveFeatures& out)
    {
        out.Clear();

        const View view = ViewOf(state, perspective);

        for (uint8_t piece = 0; piece < state.pieceBitboards.size(); ++piece)
        {
            Bitboard pieces = state.pieceBitboards[piece];
            while (pieces)
            {
                const uint8_t square = BitUtil::PopLSB(pieces);
                out.Add(FeatureIndexOf(view, Piece(piece), square));
            }
        }
    }

    void ComputeDelta(const DirtyPieces& dirty, const View& view, FeatureDelta& out)
    {
        out.Clear();

        for (size_t index = 0; index < dirty.count; ++index)
        {
            const DirtyPiece& piece = dirty.pieces[index];
            if (piece.piece == PieceType::NO_PIECE)
                continue;

            if (piece.from != NoSquare)
                out.removed[out.removedCount++] = FeatureIndexOf(view, piece.piece, piece.from);
            if (piece.to != NoSquare)
                out.added[out.addedCount++] = FeatureIndexOf(view, piece.piece, piece.to);
        }
    }
}
