#pragma once

#include <cstdint>

// Not using a class, for implicit conversion to uint16_t.
enum  ChessBoardFlags : uint16_t
{
    NO_CHESSBOARDFLAGS = 0,

    CAN_WHITE_CASTLE_KING = 1, // Bit 1
    CAN_WHITE_CASTLE_QUEEN = 2, // Bit 2
    CAN_BLACK_CASTLE_KING = 4, // Bit 3
    CAN_BLACK_CASTLE_QUEEN = 8, // Bit 4

    WHITE_TO_MOVE = 16, // Bit 5

    IS_IN_CHECK = 32, // Bit 6

    IS_GAME_OVER = 64, // Bit 7

    IS_CHECKMATE = 128, // Bit 8
    IS_STALEMATE = 256, // Bit 9
    IS_50MOVE_RULE = 512, // Bit 10
    IS_RESIGN = 1024, // Bit 11

    IS_WHITE_WIN = 2048, // Bit 12

};