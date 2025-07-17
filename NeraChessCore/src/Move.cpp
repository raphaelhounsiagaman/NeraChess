#include "Move.h"

bool Move::operator==(const Move& other) const
{
	return (
		startSquare == other.startSquare &&
		targetSquare == other.targetSquare &&

		movePieceType == other.movePieceType &&
		capturePieceType == other.capturePieceType &&
		promotionPieceType == other.promotionPieceType &&

		moveFlags == other.moveFlags
		);
};