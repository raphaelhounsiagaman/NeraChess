#pragma once

#include <cstdint>

#include "MoveFlags.h"
#include "Piece.h"

struct Move
{
public:
	bool operator==(const Move& other) const;

	Move(uint8_t start = 0, uint8_t target = 0, uint8_t flags = 0, Piece movePiece = { (uint8_t)PieceType::NO_PIECE }, Piece capturePiece = { (uint8_t)PieceType::NO_PIECE }, Piece promotionPiece = { (uint8_t)PieceType::NO_PIECE })
		: startSquare(start), targetSquare(target), moveFlags(flags), movePieceType(movePiece), capturePieceType(capturePiece), promotionPieceType(promotionPiece)
	{ }

	// MoveFlags  OR'ed together.
	uint8_t moveFlags = 0;

	uint8_t startSquare = 0;
	uint8_t targetSquare = 0;

	Piece movePieceType = { (uint8_t)PieceType::NO_PIECE };
	Piece capturePieceType = { (uint8_t)PieceType::NO_PIECE };
	Piece promotionPieceType = { (uint8_t)PieceType::NO_PIECE };

};