#include "Square.h"

namespace NeraChessEngine
{
	uint8_t Square::GetRank() const
	{
		return square >> 3;
	}

	uint8_t Square::GetFile() const
	{
		return square & 0b000111;
	}

	std::string Square::ToString() const
	{
		return 
		{
			(char)('a' + GetFile()),
			(char)('1' + GetRank())
		};
	}

	bool Square::IsValid() const
	{
		return square < 64;
	}

	bool Square::IsLightSquare() const
	{
		return ((GetFile() + GetRank()) & 1) != 0;
	}

	bool Square::ContainsSquare(Bitboard bitboard) const
	{
		return ((bitboard >> square) & 1);
	}

	bool Square::IsValidCoordinates(uint8_t file, uint8_t rank)
	{
		return ((file | rank) & ~7) == 0;
	}

	Square operator+(const Square& lhs, int rhs)
	{
		return Square(lhs.square + rhs);
	}

	Square operator+(int lhs, const Square& rhs)
	{
		return Square(lhs + rhs.square);
	}

} // namespace NeraChessEngine
