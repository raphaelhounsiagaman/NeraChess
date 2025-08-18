#include "MyBotOld.h"

Move MyBotOld::GetNextMove(const ChessBoard& board)
{
    return board.GetLegalMoves()[0];
}
