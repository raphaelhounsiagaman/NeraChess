#pragma once

#include <cstdint>

// Not using a class, for implicit conversion to uint8_t.
enum PieceTypeFlags : uint8_t
{
	NO_PIECE = 0,

	// Bit 1-3
	PAWN = 1,
	KNIGHT = 2,
	BISHOP = 3,
	ROOK = 4,
	QUEEN = 5,
	KING = 6,

	WHITE_PIECE = 8, // Bit 4
	BLACK_PIECE = 16, // Bit 5
};