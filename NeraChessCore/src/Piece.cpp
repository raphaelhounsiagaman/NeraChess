#include "Piece.h"

bool Piece::operator==(const Piece& other) const
{
	return pieceType == other.pieceType;
}

bool Piece::IsWhite() const
{
	return 
		pieceType == (uint8_t)PieceType::WHITE_PAWN ||
		pieceType == (uint8_t)PieceType::WHITE_KNIGHT ||
		pieceType == (uint8_t)PieceType::WHITE_BISHOP ||
		pieceType == (uint8_t)PieceType::WHITE_ROOK ||
		pieceType == (uint8_t)PieceType::WHITE_QUEEN ||
		pieceType == (uint8_t)PieceType::WHITE_KING;
}
