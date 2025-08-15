#pragma once

#include <cstdint>

#include "MoveFlags.h"
#include "Piece.h"

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

	bool operator==(const Move& other) const;

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