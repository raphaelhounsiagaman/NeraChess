#pragma once

#include <cstdint>

// Not using a class, for implicit conversion to uint16_t.
enum  class GameOverFlags : uint16_t
{
    IS_GAME_CONTINUE = 0,

    IS_GAME_OVER = 1, // Bit 1

    IS_WHITE_WIN = 2, // Bit 2

    IS_CHECKMATE = 4, // Bit 3
    IS_RESIGN = 8, // Bit 4
    IS_TIMEOUT = 16, // Bit 5

    IS_STALEMATE = 32, // Bit 6
    IS_REPETITION = 64, // Bit 7
    IS_50MOVE_RULE = 128, // Bit 8
    IS_INSUFFICIENT_MATERIAL = 256, // Bit 9
    IS_AGREE_ON_DRAW = 512, // Bit 10
};