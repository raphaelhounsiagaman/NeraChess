#pragma once

#include <cstdint>

namespace NeraChessEngine
{

enum PieceType : uint8_t
{
	WHITE_PAWN = 0,
	WHITE_KNIGHT,
	WHITE_BISHOP,
	WHITE_ROOK,
	WHITE_QUEEN,
	WHITE_KING,

	BLACK_PAWN,
	BLACK_KNIGHT,
	BLACK_BISHOP,
	BLACK_ROOK,
	BLACK_QUEEN,
	BLACK_KING,

	NO_PIECE = 12,
};

struct Piece
{
	Piece() = default;
	Piece(uint8_t p) : piece(p) {}

	uint8_t piece{PieceType::NO_PIECE};

	operator uint8_t() const
	{
		return piece;
	}

	Piece& operator++()
	{
		++piece;
		return *this;
	}

	Piece operator++(int)
	{
		Piece old = *this;
		++(*this);
		return old;
	}

	bool operator<(int other) const
	{
		return piece < other;
	}

	bool operator>(int other) const
	{
		return piece > other;
	}

	bool IsWhite() const
	{
		return piece < PieceType::BLACK_PAWN;
	}
	bool IsBlack() const
	{
		return piece >= PieceType::BLACK_PAWN && piece < PieceType::NO_PIECE;
	}
	bool IsDiagonalSlider() const;
	bool IsOrthogonalSlider() const;
};

Piece operator+(const Piece& lhs, uint8_t rhs);
Piece operator+(uint8_t lhs, const Piece& rhs);

} // namespace NeraChessEngine
