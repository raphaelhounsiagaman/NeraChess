#pragma once

#include "ChessPlayer.h"

#include <atomic>

class Human : public ChessPlayer
{
public:
	Human() = default;
	virtual ~Human() = default;

	virtual NeraChessEngine::Move GetNextMove(const NeraChessEngine::ChessBoard& board, const NeraChessEngine::Clock& timer) override;
	virtual void ResetGame() override { m_StopSearching = false; };
	virtual void StopSearching() override { m_StopSearching = true; }

private:
	std::atomic<bool> m_StopSearching = false;


};
