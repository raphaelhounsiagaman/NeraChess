#pragma once

#include "ChessPlayer.h"

class Bot1 : public ChessPlayer
{
public:
	Bot1() = default;
	~Bot1() override = default;

	Move GetNextMove(const ChessBoard& board) override;
};