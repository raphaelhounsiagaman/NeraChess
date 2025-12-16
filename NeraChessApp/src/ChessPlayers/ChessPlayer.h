#pragma once

#include "ChessBoard.h"
#include "Timer.h"

class ChessPlayer
{
public:

	virtual ~ChessPlayer() = default;

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Timer& timer) = 0;
	virtual void StopSearching() = 0;
};