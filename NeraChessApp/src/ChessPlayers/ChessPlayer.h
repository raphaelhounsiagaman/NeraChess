#pragma once

#include "ChessBoard.h"
#include "Clock.h"

class ChessPlayer
{
public:

	virtual ~ChessPlayer() = default;

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Clock& timer) = 0;
	virtual void ResetGame() = 0;
	virtual void StopSearching() = 0;
};