#include "Piece.h"

namespace ChessCore
{

	bool Piece::IsDiagonalSlider() const
	{
		return
			piece == PieceType::WHITE_BISHOP ||
			piece == PieceType::WHITE_QUEEN ||
			piece == PieceType::BLACK_BISHOP ||
			piece == PieceType::BLACK_QUEEN;
	}

	bool Piece::IsOrthogonalSlider() const
	{
		return
			piece == PieceType::WHITE_ROOK ||
			piece == PieceType::WHITE_QUEEN ||
			piece == PieceType::BLACK_ROOK ||
			piece == PieceType::BLACK_QUEEN;
	}

	Piece operator+(const Piece& lhs, uint8_t rhs)
	{
		return Piece(lhs.piece + rhs);
	}

	Piece operator+(uint8_t lhs, const Piece& rhs)
	{
		return Piece(lhs + rhs.piece);
	}

} // namespace ChessCore