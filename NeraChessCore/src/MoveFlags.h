#pragma once

#include <cstdint>

// Not using a class, for implicit conversion to uint8_t.
enum MoveFlags : uint8_t
{
	NO_MOVE = 0,
	IS_VALID = 1, // Bit 1
	IS_CAPTURE = 2, // Bit 2
	IS_EN_PASSENT = 4, // Bit 3
	IS_PROMOTION = 8, // Bit 4
	IS_CASTLES = 16, // Bit 5
};