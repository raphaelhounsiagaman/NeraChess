#pragma once

#include <cstdint>
#include <string>

namespace ChessCore
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

		static const Square a1;
		static const Square b1;
		static const Square c1;
		static const Square d1;
		static const Square e1;
		static const Square f1;
		static const Square g1;
		static const Square h1;

		static const Square a8;
		static const Square b8;
		static const Square c8;
		static const Square d8;
		static const Square e8;
		static const Square f8;
		static const Square g8;
		static const Square h8;

		static const Bitboard FileA;
		static const Bitboard FileH;
		static const Bitboard NotAFile;
		static const Bitboard NotHFile;
		 
		static const Bitboard Rank1;
		static const Bitboard Rank2;
		static const Bitboard Rank3;
		static const Bitboard Rank4;
		static const Bitboard Rank5;
		static const Bitboard Rank6;
		static const Bitboard Rank7;
		static const Bitboard Rank8;

	};

	Square operator+(const Square& lhs, int rhs);
	Square operator+(int lhs, const Square& rhs);

} // namespace ChessCore