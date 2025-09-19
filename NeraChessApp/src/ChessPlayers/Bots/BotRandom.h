#pragma once

#include "../ChessPlayer.h"
#include "Timer.h"

class BotRandom : public ChessPlayer
{
public:
	BotRandom() = default;
	~BotRandom() override = default;

	Move GetNextMove(const ChessBoard& board, Timer timer) override;
};