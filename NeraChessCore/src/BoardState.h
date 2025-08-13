#pragma once

#include <cstdint>
#include <array>

typedef uint64_t Bitboard;

enum class BoardStateFlags : uint8_t
{
    NONE = 0,

    CanWhiteCastleKing = 1, // Bit 1
    CanWhiteCastleQueen = 2, // Bit 2
    CanBlackCastleQueen = 4, // Bit 3
    CanBlackCastleKing = 8, // Bit 4

    CanEnPassent = 16, // Bit 5

    WhiteToMove = 32, // Bit 6
};

struct BoardState
{
    std::array<Bitboard, 12> pieceBitboards{};
    uint8_t boardStateFlags = 0;
    uint8_t enPassentFile = 8;
};