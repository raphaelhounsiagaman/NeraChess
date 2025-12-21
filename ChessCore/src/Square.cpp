#include "Square.h"

namespace ChessCore
{
	const Square Square::a1{ 0 };
	const Square Square::b1{ 1 };
	const Square Square::c1{ 2 };
	const Square Square::d1{ 3 };
	const Square Square::e1{ 4 };
	const Square Square::f1{ 5 };
	const Square Square::g1{ 6 };
	const Square Square::h1{ 7 };

	const Square Square::a8{ 56 };
	const Square Square::b8{ 57 };
	const Square Square::c8{ 58 };
	const Square Square::d8{ 59 };
	const Square Square::e8{ 60 };
	const Square Square::f8{ 61 };
	const Square Square::g8{ 62 };
	const Square Square::h8{ 63 };

	const Bitboard Square::FileA = 0x101010101010101;
	const Bitboard Square::FileH = FileA << 7;
	const Bitboard Square::NotAFile = ~FileA;
	const Bitboard Square::NotHFile = ~FileH;
	
	const Bitboard Square::Rank1 = 0b11111111;
	const Bitboard Square::Rank2 = Rank1 << (8 * 1);
	const Bitboard Square::Rank3 = Rank1 << (8 * 2);
	const Bitboard Square::Rank4 = Rank1 << (8 * 3);
	const Bitboard Square::Rank5 = Rank1 << (8 * 4);
	const Bitboard Square::Rank6 = Rank1 << (8 * 5);
	const Bitboard Square::Rank7 = Rank1 << (8 * 6);
	const Bitboard Square::Rank8 = Rank1 << (8 * 7);

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
		return square % 2 != 0;
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

} // namespace ChessCore