#pragma once

#include <cstdint>
#include <array>


namespace ChessCore
{
    typedef uint64_t Bitboard;

    enum BoardStateFlags : uint8_t
    {
        NONE = 0,

        CanWhiteCastleKing = 1 << 0, // Bit 1
        CanWhiteCastleQueen = 1 << 1, // Bit 2
        CanBlackCastleQueen = 1 << 2, // Bit 3
        CanBlackCastleKing = 1 << 3, // Bit 4

        CanEnPassent = 1 << 4, // Bit 5

        WhiteToMove = 1 << 5, // Bit 6
    };

    struct BoardState
    {
        std::array<Bitboard, 12> pieceBitboards{};
        uint8_t boardStateFlags = 0;
        uint8_t enPassantFile = 8;

        bool operator==(const BoardState& other) const;

        uint8_t GetCastlingRights() const { return boardStateFlags & 0b1111; }

        bool HasFlag(BoardStateFlags flag) const { return boardStateFlags & flag; }


    };

} // namespace ChessCore