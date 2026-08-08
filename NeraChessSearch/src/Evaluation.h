#pragma once

#include "ChessBoard.h"

#include <cstdint>

namespace NeraChessSearch::Evaluation
{
    int32_t Evaluate(const NeraChessEngine::ChessBoard& board);
}
