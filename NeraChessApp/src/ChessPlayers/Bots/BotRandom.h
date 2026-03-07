#pragma once

#include "../ChessPlayer.h"
#include "Clock.h"

class BotRandom : public ChessPlayer
{
public:
	BotRandom() = default;
	~BotRandom() override = default;

	virtual NeraChessEngine::Move GetNextMove(const NeraChessEngine::ChessBoard& board, const NeraChessEngine::Clock& timer) override;
	virtual void ResetGame() override {};
	virtual void StopSearching() override {};
};