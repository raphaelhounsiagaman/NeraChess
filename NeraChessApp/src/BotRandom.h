#pragma once

#include "ChessPlayer.h"

class BotRandom : public ChessPlayer
{
public:
	BotRandom() = default;
	~BotRandom() override = default;

	Move GetNextMove(const ChessBoard& board) override;
};