#pragma once

#include "ChessPlayer.h"

#include <mutex>


class Human : public ChessPlayer
{
public:

	Move GetNextMove(const ChessBoard& board, Timer timer) override;

	Bitboard GetPossibleMoves();
	void SetSelectedSquare(uint8_t square);

private:
	
	std::mutex m_SelectedSquareMutex;
	uint8_t m_SelectedSquare = 64;

	std::mutex m_PossibleMovesMutex;
	Bitboard m_PossibleMoves = 0ULL;
};