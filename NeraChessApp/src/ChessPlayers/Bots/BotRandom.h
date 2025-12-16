#pragma once

#include "../ChessPlayer.h"
#include "Timer.h"

class BotRandom : public ChessPlayer
{
public:
	BotRandom() = default;
	~BotRandom() override = default;

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Timer& timer) override;
	virtual void StopSearching() override {};
};