#pragma once

#include <cstdint>

// Not using a class, for implicit conversion to uint8_t.
enum class PieceType : uint8_t
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
	uint8_t pieceType = 0;

	bool operator==(const Piece& other) const
	{
		return pieceType == other.pieceType;
	}

	bool IsWhite() const
	{
		return
			pieceType == (uint8_t)PieceType::WHITE_PAWN ||
			pieceType == (uint8_t)PieceType::WHITE_KNIGHT ||
			pieceType == (uint8_t)PieceType::WHITE_BISHOP ||
			pieceType == (uint8_t)PieceType::WHITE_ROOK ||
			pieceType == (uint8_t)PieceType::WHITE_QUEEN ||
			pieceType == (uint8_t)PieceType::WHITE_KING;
	}
};