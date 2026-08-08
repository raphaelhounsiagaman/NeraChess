#pragma once

#include "Clock.h"
#include "SearchEngine.h"

#include <chrono>

namespace NeraChessSearch::TimeManagement
{
    SearchLimits CalculateLimits(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Clock& clock);
    SearchLimits CalculateLimits(const NeraChessEngine::ChessBoard& board,
        std::chrono::milliseconds remaining, std::chrono::milliseconds increment,
        int movesToGo = 0);
}
