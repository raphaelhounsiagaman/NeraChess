#include "Util.h"

#include <bit>



namespace ChessUtil
{
	/*
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

	*/

	uint8_t SquareToRank(uint8_t square)
	{
		return square >> 3;
	}

	uint8_t SquareToFile(uint8_t square)
	{
		return square & 0b000111;
	}

	uint8_t CoordsToSquare(uint8_t file, uint8_t rank)
	{
		return rank * 8 + file;
	}

	bool LightSquare(uint8_t file, uint8_t rank)
	{
		return (file + rank) % 2;
	}

	bool LightSquare(uint8_t square)
	{
		return (SquareToFile(square) + SquareToRank(square)) % 2;
	}

	bool IsValidCoordinate(uint8_t file, uint8_t rank)
	{
		return file >= 0 && file < 8 && rank >= 0 && rank < 8;
	}

	Bitboard PawnAttacks(Bitboard pawns, bool isWhite)
	{

		// Pawn attacks are calculated like so: (example given with white to move)

		// The first half of the attacks are calculated by shifting all pawns north-east: northEastAttacks = pawnBitboard << 9
		// Note that pawns on the h file will be wrapped around to the a file, so then mask out the a file: northEastAttacks &= notAFile
		// (Any pawns that were originally on the a file will have been shifted to the b file, so a file should be empty).

		// The other half of the attacks are calculated by shifting all pawns north-west. This time the h file must be masked out.
		// Combine the two halves to get a bitboard with all the pawn attacks: northEastAttacks | northWestAttacks

		if (isWhite)
		{
			return ((pawns << 9) & NotAFile) | ((pawns << 7) & NotHFile);
		}

		return ((pawns >> 7) & NotAFile) | ((pawns >> 9) & NotHFile);
	}

	bool ContainsSquare(Bitboard bitboard, uint8_t square)
	{
		return ((bitboard >> square) & 1) != 0;
	}

	

}

namespace PieceUtil
{
	bool IsDiagonalSlider(PieceType piece)
	{
		return 
			piece == PieceType::WHITE_BISHOP ||
			piece == PieceType::WHITE_QUEEN  ||
			piece == PieceType::BLACK_BISHOP ||
			piece == PieceType::BLACK_QUEEN;
	}

	bool IsOrthogonalSlider(PieceType piece)
	{
		return
			piece == PieceType::WHITE_ROOK  ||
			piece == PieceType::WHITE_QUEEN ||
			piece == PieceType::BLACK_ROOK  ||
			piece == PieceType::BLACK_QUEEN;
	}

}

namespace BitUtil
{
	uint8_t TrailingZeroCount(uint64_t value)
	{
		return std::countr_zero(value);
	}

	bool IsPow2(uint64_t value)
	{
		return (value & (value - 1)) == 0 && value > 0;
	}

	uint8_t PopLSB(uint64_t& value)
	{
		uint8_t i = TrailingZeroCount(value);
		value &= (value - 1);
		return i;
	}

	uint8_t GetLSBIndex(uint64_t value)
	{
		return TrailingZeroCount(value);
	}

	uint8_t PopCnt(uint64_t value)
	{
		return std::popcount(value);
	}

	Bitboard Shift(Bitboard bitboard, int numSquaresToShift)
	{
		if (numSquaresToShift > 0)
		{
			return bitboard << numSquaresToShift;
		}
		else
		{
			return bitboard >> -numSquaresToShift;
		}

	}

}


