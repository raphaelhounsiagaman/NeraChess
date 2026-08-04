#pragma once

#include "ChessBoard.h"

namespace NeraChessSearch::MoveOrdering
{
    int StaticExchangeEvaluation(const NeraChessEngine::ChessBoard& board,
        NeraChessEngine::Move move);
}
