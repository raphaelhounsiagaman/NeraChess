#pragma once

#include <cstdint>

#include <string>

#include "PieceType.h"

typedef uint64_t Bitboard;

namespace ChessUtil
{
    const int a1 = 0;
    const int b1 = 1;
    const int c1 = 2;
    const int d1 = 3;
    const int e1 = 4;
    const int f1 = 5;
    const int g1 = 6;
    const int h1 = 7;

    const int a8 = 56;
    const int b8 = 57;
    const int c8 = 58;
    const int d8 = 59;
    const int e8 = 60;
    const int f8 = 61;
    const int g8 = 62;
    const int h8 = 63;

    const Bitboard FileA = 0x101010101010101;
    const Bitboard FileH = FileA << 7;
    const Bitboard NotAFile = ~FileA;
    const Bitboard NotHFile = ~FileH;

    const Bitboard Rank1 = 0b11111111;
    const Bitboard Rank2 = Rank1 << (8 * 1);
    const Bitboard Rank3 = Rank1 << (8 * 2);
    const Bitboard Rank4 = Rank1 << (8 * 3);
    const Bitboard Rank5 = Rank1 << (8 * 4);
    const Bitboard Rank6 = Rank1 << (8 * 5);
    const Bitboard Rank7 = Rank1 << (8 * 6);
    const Bitboard Rank8 = Rank1 << (8 * 7);

    uint8_t SquareToRank(uint8_t square);
    uint8_t SquareToFile(uint8_t square);

    uint8_t CoordsToSquare(uint8_t file, uint8_t rank);

    std::string SquareAsString(uint8_t square);
    std::string CoordsAsString(uint8_t file, uint8_t rank);

    bool LightSquare(uint8_t file, uint8_t rank);
    bool LightSquare(uint8_t squareIndex);

    bool IsValidCoordinate(uint8_t file, uint8_t rank);
    
	Bitboard PawnAttacks(Bitboard pawns, bool isWhite);

    bool ContainsSquare(Bitboard bitboard, uint8_t square);


}

namespace PieceUtil
{
    bool IsDiagonalSlider(PieceType piece);
    bool IsOrthogonalSlider(PieceType piece);


}

namespace BitUtil
{

    uint8_t TrailingZeroCount(uint64_t value);
    
    bool IsPow2(uint64_t value);

    uint8_t PopLSB(uint64_t& value);
    uint8_t GetLSBIndex(uint64_t value);

    uint8_t PopCnt(uint64_t value);

    Bitboard Shift(Bitboard bitboard, int numSquaresToShift);

}