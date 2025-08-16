#include "Bot1.h"

#include <random>

Move Bot1::GetNextMove(const ChessBoard& board)
{
    std::random_device rd;

    std::mt19937 gen(rd());

    std::uniform_int_distribution<> dist(0, (board.GetLegalMoves().size() - 1));

    uint16_t moveIndex = dist(gen);

    return board.GetLegalMoves()[moveIndex];
}
