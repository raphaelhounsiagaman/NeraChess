#pragma once

#include <cstdint>

#include "MoveFlags.h"
#include "PieceTypeFlags.h"
#include "Piece.h"

struct Move
{
public:
	bool operator==(const Move& other) const;

	// MoveFlags  OR'ed together.
	uint8_t moveFlags = 0;

	uint8_t startSquare = 0;
	uint8_t targetSquare = 0;

	Piece movePieceType = { PieceTypeFlags::NO_PIECE };
	Piece capturePieceType = { PieceTypeFlags::NO_PIECE };
	Piece promotionPieceType = { PieceTypeFlags::NO_PIECE };

};