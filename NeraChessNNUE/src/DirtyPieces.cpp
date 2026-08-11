#include "DirtyPieces.h"

namespace NeraChessNNUE
{
    using namespace NeraChessEngine;

    Piece CapturedPieceOf(const ChessBoard& board, const Move& move)
    {
        const uint8_t flags = move.GetMoveFlags();
        if (!(flags & MoveFlags::IS_CAPTURE))
            return PieceType::NO_PIECE;
        if (flags & MoveFlags::IS_EN_PASSANT)
        {
            return move.GetMovePiece().IsWhite()
                ? Piece(PieceType::BLACK_PAWN)
                : Piece(PieceType::WHITE_PAWN);
        }
        return board.GetPiece(move.GetTargetSquare());
    }

    DirtyPieces DescribeMove(const ChessBoard& board, const Move& move)
    {
        return DescribeMove(move, CapturedPieceOf(board, move));
    }

    DirtyPieces DescribeMove(const Move& move, Piece capturedPiece)
    {
        DirtyPieces dirty;
        if (!move)
            return dirty;

        const uint8_t from = move.GetStartSquare();
        const uint8_t to = move.GetTargetSquare();
        const Piece moving = move.GetMovePiece();
        const uint8_t flags = move.GetMoveFlags();
        const bool white = moving.IsWhite();

        if ((flags & MoveFlags::IS_CAPTURE) && capturedPiece != PieceType::NO_PIECE)
        {
            // An en-passant capture removes a pawn from the square the capturer
            // passes over, not from the square it lands on.
            const uint8_t captureSquare = (flags & MoveFlags::IS_EN_PASSANT)
                ? static_cast<uint8_t>(white ? to - 8 : to + 8)
                : to;
            dirty.Add(capturedPiece, captureSquare, NoSquare);
        }

        if (flags & MoveFlags::IS_PROMOTION)
        {
            // The pawn ceases to exist and a new piece appears, so these are two
            // independent feature changes rather than one relocation.
            dirty.Add(moving, from, NoSquare);
            dirty.Add(move.GetPromoPiece(), NoSquare, to);
        }
        else
        {
            dirty.Add(moving, from, to);
        }

        if (flags & MoveFlags::IS_CASTLES)
        {
            // ChessBoard::MakeMove encodes castling as a two-square king move and
            // identifies the side by the king's destination file: c-file for
            // queenside, g-file for kingside.
            const bool queenSide = (to % 8) == 2;
            const uint8_t backRank = white ? 0 : 56;
            const uint8_t rookFrom = static_cast<uint8_t>(backRank + (queenSide ? 0 : 7));
            const uint8_t rookTo = static_cast<uint8_t>(backRank + (queenSide ? 3 : 5));
            dirty.Add(white ? PieceType::WHITE_ROOK : PieceType::BLACK_ROOK, rookFrom, rookTo);
        }

        return dirty;
    }
}
