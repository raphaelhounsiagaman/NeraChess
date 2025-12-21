#pragma once

#include "../ChessPlayer.h"
#include "Clock.h"

class BotRandom : public ChessPlayer
{
public:
	BotRandom() = default;
	~BotRandom() override = default;

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Clock& timer) override;
	virtual void ResetGame() override {};
	virtual void StopSearching() override {};
};