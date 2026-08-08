#include "Move.h"

namespace NeraChessEngine
{

Move::Move(Square startSquare, Square targetSquare, Piece movePiece, Piece promoPiece, uint8_t flags)
{

	move = 1 | ((startSquare & 0x3F) << 1) | ((targetSquare & 0x3F) << 7) | ((movePiece & 0xF) << 13) |
		   ((promoPiece & 0xF) << 17) | ((flags & 0xFF) << 21);
}

std::string Move::ToUCI() const
{
	if (!move)
		return "0000";

	std::string promoPieceStr = "";

	Piece promoPiece = GetPromoPiece();

	if (promoPiece != 0)
	{
		switch (promoPiece)
		{
		case PieceType::WHITE_QUEEN:
		case PieceType::BLACK_QUEEN:
			promoPieceStr = "q";
			break;
		case PieceType::WHITE_ROOK:
		case PieceType::BLACK_ROOK:
			promoPieceStr = "r";
			break;
		case PieceType::WHITE_BISHOP:
		case PieceType::BLACK_BISHOP:
			promoPieceStr = "b";
			break;
		case PieceType::WHITE_KNIGHT:
		case PieceType::BLACK_KNIGHT:
			promoPieceStr = "n";
			break;
		default:
			break;
		}
	}

	return GetStartSquare().ToString() + GetTargetSquare().ToString() + promoPieceStr;
}

} // namespace NeraChessEngine
