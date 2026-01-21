#include "Move.h"

namespace ChessCore
{

	Move::Move(Square startSquare, Square targetSquare, Piece movePiece, Piece promoPiece, uint8_t flags)
	{
		
		move =
			1 |
			((startSquare & 0x3F) << 1) |
			((targetSquare & 0x3F) << 7) |
			((movePiece & 0xF) << 13) |
			((promoPiece & 0xF) << 17) |
			((flags & 0xFF) << 21);
		
	}

	Move::Move(const std::string& moveStr)
	{
		Square startSquare(moveStr[0] - 'a', moveStr[1] - '1');
		Square targetSquare(moveStr[2] - 'a', moveStr[3] - '1');

		Piece promoPiece = 0;

		if (moveStr.length() == 5)
		{
			switch (moveStr[4])
			{
			case 'q':
				promoPiece = PieceType::WHITE_QUEEN;
				break;
			case 'r':
				promoPiece = PieceType::WHITE_ROOK;
				break;
			case 'b':
				promoPiece = PieceType::WHITE_BISHOP;
				break;
			case 'n':
				promoPiece = PieceType::WHITE_KNIGHT;
				break;
			default:
				break;
			}
		}

		move = 1 |
			((startSquare & 0x3F) << 1) |
			((targetSquare & 0x3F) << 7) |
			((0 & 0xF) << 13) |
			((promoPiece & 0xF) << 17) |
			((0 & 0xFF) << 21);
	}

	std::string Move::ToUCI() const
	{
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

} // namespace ChessCore