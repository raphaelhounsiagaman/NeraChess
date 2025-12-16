#pragma once

#include <array>
#include <random>

#include "ChessBoard.h"

namespace ChessCore
{
    struct  Zobrist
    {
        static uint64_t CalculateZobristKey(ChessBoard board);

        static const std::array<std::array<uint64_t, 64>, 12> piecesArray;
        // Each player has 4 possible castling right states: none, queenside, kingside, both.
        // So, taking both sides into account, there are 16 possible states.
        static const std::array<uint64_t, 16> castlingRights;
        // En passant file (0 = no ep).
        //  Rank does not need to be specified since side to move is included in key
        static const std::array<uint64_t, 9> enPassantFile;
        static const uint64_t sideToMove;

    private:
        static std::mt19937_64 rng;

	    template<size_t Size>
        static std::array<uint64_t, Size> GetRandomArray();

        template<size_t SizeX, size_t SizeY>
        static std::array<std::array<uint64_t, SizeY>, SizeX> GetRandom2DArray();

    };

} // namespace ChessCore

