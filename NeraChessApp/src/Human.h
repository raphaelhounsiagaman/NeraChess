#pragma once

#include "ChessPlayer.h"

#include <mutex>

class Human : public ChessPlayer
{
public:

	Move GetNextMove(const ChessBoard& board) override;

	Bitboard GetPossibleMoves() { std::lock_guard<std::mutex> lock(m_PossibleMovesMutex); return m_PossibleMoves; }
	void SetSelectedSquare(uint8_t square) { std::lock_guard<std::mutex> lock(m_SelectedSquareMutex); m_SelectedSquare = square; }

private:
	
	std::mutex m_SelectedSquareMutex;
	uint8_t m_SelectedSquare;

	std::mutex m_PossibleMovesMutex;
	Bitboard m_PossibleMoves = 0ULL;
};