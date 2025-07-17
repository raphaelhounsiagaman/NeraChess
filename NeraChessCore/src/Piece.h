#pragma once

#include <cstdint>

#include "PieceTypeFlags.h"

struct Piece
{
	// PieceTypeFlags OR'ed together.
	uint8_t pieceType = 0;

	bool IsType(PieceTypeFlags flag) const;
	bool IsColor(PieceTypeFlags flag) const;

	bool operator==(const Piece& other) const;
};