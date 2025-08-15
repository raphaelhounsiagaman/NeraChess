#pragma once

#include "NeraChessCore.h"

class ChessPlayer
{
public:

	virtual ~ChessPlayer() = default;

	virtual Move GetNextMove(const ChessBoard& board) = 0;

};