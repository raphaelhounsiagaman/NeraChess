#include "Piece.h"

namespace ChessCore
{

	namespace PieceUtil
	{
		bool IsWhite(Piece piece)
		{
			return !((piece & 0b1000) || ((piece & 0b0110) == 0b0110));
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

} // namespace ChessCore