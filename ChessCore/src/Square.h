#pragma once

#include <cstdint>
#include <string>

namespace ChessCore
{

	using Square = uint8_t;
	using Bitboard = uint64_t;

	namespace SquareUtil
	{
		constexpr Square a1 = 0;
		constexpr Square b1 = 1;
		constexpr Square c1 = 2;
		constexpr Square d1 = 3;
		constexpr Square e1 = 4;
		constexpr Square f1 = 5;
		constexpr Square g1 = 6;
		constexpr Square h1 = 7;

		constexpr Square a8 = 56;
		constexpr Square b8 = 57;
		constexpr Square c8 = 58;
		constexpr Square d8 = 59;
		constexpr Square e8 = 60;
		constexpr Square f8 = 61;
		constexpr Square g8 = 62;
		constexpr Square h8 = 63;

		constexpr Bitboard FileA = 0x101010101010101;
		constexpr Bitboard FileH = FileA << 7;
		constexpr Bitboard NotAFile = ~FileA;
		constexpr Bitboard NotHFile = ~FileH;

		constexpr Bitboard Rank1 = 0b11111111;
		constexpr Bitboard Rank2 = Rank1 << (8 * 1);
		constexpr Bitboard Rank3 = Rank1 << (8 * 2);
		constexpr Bitboard Rank4 = Rank1 << (8 * 3);
		constexpr Bitboard Rank5 = Rank1 << (8 * 4);
		constexpr Bitboard Rank6 = Rank1 << (8 * 5);
		constexpr Bitboard Rank7 = Rank1 << (8 * 6);
		constexpr Bitboard Rank8 = Rank1 << (8 * 7);

		uint8_t CoordsToSquare(uint8_t file, uint8_t rank);

		uint8_t GetRank(Square square);

		uint8_t GetFile(Square square);

		bool IsValidSquare(Square square);

		bool IsValidCoordinate(uint8_t file, uint8_t rank);

		bool LightSquare(uint8_t file, uint8_t rank);

		bool LightSquare(uint8_t square);

		bool ContainsSquare(Bitboard bitboard, uint8_t square);

		std::string CoordsAsString(uint8_t file, uint8_t rank);

		std::string SquareAsString(uint8_t square);
	}


} // namespace ChessCore