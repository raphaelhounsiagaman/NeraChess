#pragma once

#include "ChessPlayer.h"

#include <atomic>

class Human : public ChessPlayer
{
public:
	Human() = default;
	virtual ~Human() = default;

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Timer& timer) override;
	virtual void StopSearching() override { m_StopSearching = true; }

private:
	std::atomic<bool> m_StopSearching = false;


};
