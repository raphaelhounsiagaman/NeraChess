#pragma once

#include <cstdint>

using Piece = uint8_t;

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

namespace PieceUtil
{
	bool IsWhite(Piece piece);

	bool IsDiagonalSlider(Piece piece);


	bool IsOrthogonalSlider(Piece piece);

}