#pragma once

#include "../ChessPlayer.h"
#include "Timer.h"

class FirstNNBot : public ChessPlayer
{
public:

	FirstNNBot();

	Move GetNextMove(const ChessBoard& givenBoard, Timer timer) override;

private:

	double Minimax(ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta);

	double EvaluateBoard(const ChessBoard& board, bool whiteToMove) const;

	static void SortMoves(const ChessBoard& board, MoveList& moves);

private:

	static constexpr float m_PieceValues[12] = {
		10.0f, // WHITE_PAWN
		30.0f, // WHITE_KNIGHT
		32.0f, // WHITE_BISHOP
		50.0f, // WHITE_ROOK
		90.0f, // WHITE_QUEEN
		10000.0f, // WHITE_KING
		-10.0f, // BLACK_PAWN
		-30.0f, // BLACK_KNIGHT
		-32.0f, // BLACK_BISHOP
		-50.0f, // BLACK_ROOK
		-90.0f, // BLACK_QUEEN
		-10000.0f // BLACK_KING
	};

	

};
