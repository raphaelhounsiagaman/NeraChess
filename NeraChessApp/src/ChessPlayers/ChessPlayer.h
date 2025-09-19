#pragma once

#include "ChessBoard.h"
#include "Timer.h"

class ChessPlayer
{
public:

	virtual ~ChessPlayer() = default;

	virtual Move GetNextMove(const ChessBoard& board, Timer timer) = 0;

};