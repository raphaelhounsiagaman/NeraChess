#pragma once

#include <cstdint>
#include <string>

namespace NeraChessEngine
{
	using Bitboard = uint64_t;

	struct Square
	{
		Square() = default;
		Square(uint8_t file, uint8_t rank) { square = file + rank * 8; };
		Square(uint8_t s) : square(s) {}

		uint8_t square{ 0 };

		operator uint8_t() const { return square; }

		Square& operator++()
		{
			++square;
			return *this;
		}

		Square operator++(int)
		{
			Square old = *this;
			++(*this);
			return old;
		}

		bool operator<(int other) const
		{
			return square < other;
		}

		bool operator>(int other) const
		{
			return square > other;
		}

		uint8_t GetRank() const;
		uint8_t GetFile() const;

		std::string ToString() const; 

		bool IsValid() const;
		bool IsLightSquare() const;
		bool ContainsSquare(Bitboard board) const;

		static bool IsValidCoordinates(uint8_t file, uint8_t rank);

		enum : uint8_t
		{
			a1 = 0, b1 = 1, c1 = 2, d1 = 3, e1 = 4, f1 = 5, g1 = 6, h1 = 7,
			a8 = 56, b8 = 57, c8 = 58, d8 = 59, e8 = 60, f8 = 61, g8 = 62, h8 = 63,
		};

		static constexpr Bitboard FileA = 0x0101010101010101ULL;
		static constexpr Bitboard FileH = FileA << 7;
		static constexpr Bitboard NotAFile = ~FileA;
		static constexpr Bitboard NotHFile = ~FileH;

		static constexpr Bitboard Rank1 = 0xFFULL;
		static constexpr Bitboard Rank2 = Rank1 << (8 * 1);
		static constexpr Bitboard Rank3 = Rank1 << (8 * 2);
		static constexpr Bitboard Rank4 = Rank1 << (8 * 3);
		static constexpr Bitboard Rank5 = Rank1 << (8 * 4);
		static constexpr Bitboard Rank6 = Rank1 << (8 * 5);
		static constexpr Bitboard Rank7 = Rank1 << (8 * 6);
		static constexpr Bitboard Rank8 = Rank1 << (8 * 7);

	};

	Square operator+(const Square& lhs, int rhs);
	Square operator+(int lhs, const Square& rhs);

} // namespace NeraChessEngine
