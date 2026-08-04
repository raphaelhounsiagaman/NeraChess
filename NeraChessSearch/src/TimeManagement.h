#pragma once

#include "Clock.h"
#include "SearchEngine.h"

namespace NeraChessSearch::TimeManagement
{
    SearchLimits CalculateLimits(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Clock& clock);
}
