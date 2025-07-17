#include "Piece.h"

bool Piece::IsType(PieceTypeFlags flag) const
{
	return (pieceType & 0b111) == flag;
}

bool Piece::IsColor(PieceTypeFlags flag) const
{
	return pieceType & flag;
}

bool Piece::operator==(const Piece& other) const
{
	return pieceType == other.pieceType;
}