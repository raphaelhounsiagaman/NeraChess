#include "Square.h"

namespace SquareUtil
{

	uint8_t CoordsToSquare(uint8_t file, uint8_t rank)
	{
		return rank * 8 + file;
	}

	uint8_t GetRank(Square square)
	{
		return square >> 3;
	}

	uint8_t GetFile(Square square)
	{
		return square & 0b000111;
	}

	bool IsValidSquare(Square square)
	{
		return square < 64;
	}

	bool IsValidCoordinate(uint8_t file, uint8_t rank)
	{
		return ((file | rank) & ~7) == 0;
	}

	bool LightSquare(uint8_t file, uint8_t rank)
	{
		return !((file ^ rank) & 1);
	}

	bool LightSquare(uint8_t square)
	{
		return 1 ^ ((square ^ (square >> 3)) & 1);;
	}

	bool ContainsSquare(Bitboard bitboard, uint8_t square)
	{
		return ((bitboard >> square) & 1);
	}

	std::string CoordsAsString(uint8_t file, uint8_t rank)
	{
		return {
			static_cast<char>('a' + file),
			static_cast<char>('1' + rank)
		};
	}

	std::string SquareAsString(uint8_t square)
	{
		uint8_t file = square % 8;
		uint8_t rank = square / 8;

		return {
			static_cast<char>('a' + file),
			static_cast<char>('1' + rank)
		};
	}


}