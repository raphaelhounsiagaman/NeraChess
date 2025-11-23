#include "BotRandom.h"

#include <random>

Move BotRandom::GetNextMove(const ChessBoard& board, Timer timer)
{
    std::random_device rd;

    std::mt19937 gen(rd());

    std::uniform_int_distribution<size_t> dist(0, (board.GetLegalMoves().size() - 1));

    uint8_t moveIndex = (uint8_t)dist(gen);

    return board.GetLegalMoves()[moveIndex];
}
