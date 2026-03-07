#pragma once

#include "ChessBoard.h"
#include "Clock.h"

class ChessPlayer
{
public:

	virtual ~ChessPlayer() = default;

	virtual NeraChessEngine::Move GetNextMove(const NeraChessEngine::ChessBoard& board, const NeraChessEngine::Clock& timer) = 0;
	virtual void ResetGame() = 0;
	virtual void StopSearching() = 0;
};