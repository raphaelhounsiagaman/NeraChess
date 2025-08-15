#pragma once

#include <cstdint>

#include "Piece.h"

enum class MoveFlags : uint8_t
{
	NO_MOVE = 0,
	IS_VALID = 1, // Bit 1
	IS_CAPTURE = 2, // Bit 2
	IS_EN_PASSANT = 4, // Bit 3
	IS_PROMOTION = 8, // Bit 4
	IS_CASTLES = 16, // Bit 5
	PAWN_TWO_UP = 32, // Bit 6
};


struct Move
{
public:

	Move() = default;

	Move(uint8_t flags)
		: moveFlags(flags)
	{ }

	Move(uint8_t start, uint8_t target, uint8_t flags = 0, Piece movePiece = { (uint8_t)PieceType::NO_PIECE }, Piece capturePiece = { (uint8_t)PieceType::NO_PIECE }, Piece promotionPiece = { (uint8_t)PieceType::NO_PIECE })
		: startSquare(start), targetSquare(target), moveFlags(flags), movePieceType(movePiece), capturePieceType(capturePiece), promotionPieceType(promotionPiece)
	{ }

	bool operator==(const Move& other) const
	{
		return (
			startSquare == other.startSquare &&
			targetSquare == other.targetSquare &&

			movePieceType == other.movePieceType &&
			capturePieceType == other.capturePieceType &&
			promotionPieceType == other.promotionPieceType &&

			moveFlags == other.moveFlags
			);
	}

	explicit operator bool() const {
		return moveFlags != 0;
	}

	// MoveFlags  OR'ed together.
	uint8_t moveFlags = 0;

	uint8_t startSquare = 0;
	uint8_t targetSquare = 0;

	Piece movePieceType = { (uint8_t)PieceType::NO_PIECE };
	Piece capturePieceType = { (uint8_t)PieceType::NO_PIECE };
	Piece promotionPieceType = { (uint8_t)PieceType::NO_PIECE };

};