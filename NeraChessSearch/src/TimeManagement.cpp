#include "TimeManagement.h"

#include <algorithm>
#include <chrono>

namespace NeraChessSearch::TimeManagement
{
    using namespace std::chrono;

    SearchLimits CalculateLimits(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Clock& clock)
    {
        const bool white = board.GetBoardState().HasFlag(
            NeraChessEngine::BoardStateFlags::WhiteToMove);
        return CalculateLimits(board, clock.GetRemaining(white), clock.GetIncrement());
    }

    SearchLimits CalculateLimits(const NeraChessEngine::ChessBoard& board,
        milliseconds remaining, milliseconds increment, int movesToGo)
    {
        remaining = std::max(milliseconds{ 1 }, remaining);
        increment = std::max(milliseconds{ 0 }, increment);
        const milliseconds safety = std::clamp(remaining / 20,
            milliseconds{ 20 }, milliseconds{ 500 });
        const milliseconds available = std::max(milliseconds{ 1 }, remaining - safety);

        const uint16_t fullMove = board.GetFullMoveClock();
        const int estimatedMovesLeft = movesToGo > 0
            ? std::clamp(movesToGo, 1, 100)
            : (fullMove < 20 ? 30 : (fullMove < 40 ? 24 : 18));
        milliseconds softTime = remaining / estimatedMovesLeft + increment * 3 / 4;
        softTime = std::max(milliseconds{ 20 }, softTime);
        // Never plan to spend more than a quarter of the remaining clock on a
        // single move, however few moves the estimator thinks are left. This
        // scales with the time control, unlike a fixed millisecond ceiling.
        softTime = std::min({ softTime, available, remaining / 4 });

        milliseconds hardTime = std::max(softTime, softTime * 3 / 2 + milliseconds{ 50 });
        hardTime = std::min(hardTime, available);
        softTime = std::min(softTime, hardTime);

        SearchLimits limits;
        limits.maxDepth = MAX_PLY - 1;
        limits.softTime = softTime;
        limits.hardTime = hardTime;
        return limits;
    }
}
