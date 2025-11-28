#include "Move.h"

namespace MoveUtil
{
	Move UCIToMove(const std::string& moveStr)
	{
		Square fromSquare = SquareUtil::CoordsToSquare(moveStr[0] - 'a', moveStr[1] - '1');
		Square targetSquare = SquareUtil::CoordsToSquare(moveStr[2] - 'a', moveStr[3] - '1');

		Piece promoPiece = 0;

		if (moveStr.length() == 5)
		{
			switch (moveStr[4])
			{
			case 'q':
				promoPiece = PieceType::WHITE_QUEEN; // color will be determined later
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

		return CreateMove(fromSquare, targetSquare, 0, promoPiece, 0);
	}

	std::string MoveToUCI(Move move)
	{
		std::string promoPieceStr = "";

		Piece promoPiece = GetPromoPiece(move);

		if (promoPiece != PieceType::NO_PIECE)
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

		return SquareUtil::SquareAsString(GetFromSquare(move)) + SquareUtil::SquareAsString(GetTargetSquare(move)) + promoPieceStr;
	}
}