#pragma once

#include <cstdint>

#include "PieceType.h"

struct Piece
{
	uint8_t pieceType = 0;

	bool operator==(const Piece& other) const;

	bool IsWhite() const;
};