#include "Piece.h"

namespace PieceUtil
{
	bool IsWhite(Piece piece)
	{
		return
			piece == WHITE_PAWN ||
			piece == WHITE_KNIGHT ||
			piece == WHITE_BISHOP ||
			piece == WHITE_ROOK ||
			piece == WHITE_QUEEN ||
			piece == WHITE_KING;
	}

	bool IsDiagonalSlider(Piece piece)
	{
		return
			piece == PieceType::WHITE_BISHOP ||
			piece == PieceType::WHITE_QUEEN ||
			piece == PieceType::BLACK_BISHOP ||
			piece == PieceType::BLACK_QUEEN;
	}


	bool IsOrthogonalSlider(Piece piece)
	{
		return
			piece == PieceType::WHITE_ROOK ||
			piece == PieceType::WHITE_QUEEN ||
			piece == PieceType::BLACK_ROOK ||
			piece == PieceType::BLACK_QUEEN;
	}

}