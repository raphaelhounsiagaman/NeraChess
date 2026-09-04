#pragma once

#include "ChessBoard.h"

namespace NeraChessSearch::MoveOrdering
{
    // Overload for a caller that already knows the captured piece (SortMoves derives it for
    // ordering anyway), so the exchange doesn't have to re-scan the board to answer the same
    // question the caller just answered.
    int StaticExchangeEvaluation(const NeraChessEngine::ChessBoard& board,
        NeraChessEngine::Move move, NeraChessEngine::Piece capturedPiece);

    int StaticExchangeEvaluation(const NeraChessEngine::ChessBoard& board,
        NeraChessEngine::Move move);
}
