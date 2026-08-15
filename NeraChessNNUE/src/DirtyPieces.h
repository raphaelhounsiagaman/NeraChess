#pragma once

#include "NnueCommon.h"

#include "ChessBoard.h"
#include "Move.h"
#include "Piece.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace NeraChessNNUE
{
    // Sentinel used when a dirty piece only appears (from == NoSquare) or only
    // disappears (to == NoSquare) instead of moving between two squares.
    inline constexpr uint8_t NoSquare = 64;

    // One piece changing where it sits on the board.
    //
    //   moved       from and to are both real squares
    //   captured    to == NoSquare
    //   promoted    the pawn is removed and the promotion piece is added, so a
    //               promotion produces two entries rather than one
    struct DirtyPiece
    {
        NeraChessEngine::Piece piece{ NeraChessEngine::PieceType::NO_PIECE };
        uint8_t from = NoSquare;
        uint8_t to = NoSquare;
    };

    // A move disturbs at most three pieces: the moving piece, a captured
    // piece, and the rook of a castling move. Promotions split the moving
    // piece into a removal and an addition, which still fits in four entries.
    inline constexpr size_t MaxDirtyPieces = 4;

    struct DirtyPieces
    {
        std::array<DirtyPiece, MaxDirtyPieces> pieces{};
        size_t count = 0;

        void Add(NeraChessEngine::Piece piece, uint8_t from, uint8_t to)
        {
            if (count >= MaxDirtyPieces)
                return;
            pieces[count++] = DirtyPiece{ piece, from, to };
        }

        void Clear() { count = 0; }
    };

    // The piece a move captures, or NO_PIECE. Derived exactly the way
    // ChessBoard::MakeMove derives it, including the en-passant case where the
    // captured pawn does not stand on the target square.
    //
    // Must be called before the move is made.
    NeraChessEngine::Piece CapturedPieceOf(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Move& move);

    // Derives the dirty-piece list for a move so the accumulator can be
    // updated incrementally instead of recomputed from scratch.
    //
    // The move encoding carries everything except the captured piece type, so
    // the caller supplies it. Pass NO_PIECE for quiet moves.
    DirtyPieces DescribeMove(const NeraChessEngine::Move& move,
        NeraChessEngine::Piece capturedPiece);

    // Convenience overload that reads the captured piece from the board.
    // Must be called before the move is made.
    DirtyPieces DescribeMove(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Move& move);
}
