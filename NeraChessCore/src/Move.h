#pragma once

#include <cstdint>
#include <string>

#include "Piece.h"
#include "Square.h"

/*
* 
* | Bits  | Purpose         |
* | ----- | --------------- |
* | 0     | validation      |
* | 1–6   | from square     |
* | 7–12  | to square       |
* | 13–16 | move piece      |
* | 17–20 | promotion piece |
* | 21–28 | move flags      |
* | 29–31 | unused		    |
* 
*/
using Move = uint32_t;

enum MoveFlags : uint8_t
{
	NO_MOVE = 0,

	IS_CAPTURE = 1 << 0, // Bit 1
	IS_EN_PASSANT = 1 << 1, // Bit 2
	IS_PROMOTION = 1 << 2, // Bit 3
	IS_CASTLES = 1 << 3, // Bit 4
	PAWN_TWO_UP = 1 << 4, // Bit 5
};

namespace MoveUtil
{
	
	constexpr Move CreateMove(uint8_t from, uint8_t target, uint8_t move = 0, uint8_t promo = 0, uint8_t flags = 0)
	{
		return
			((static_cast<Move>(1)))	 			    |
			((static_cast<Move>(from)   & 0x3F) <<  1)  |
			((static_cast<Move>(target) & 0x3F) <<  7)  |
			((static_cast<Move>(move)   &  0xF) << 13)  |
			((static_cast<Move>(promo)  &  0xF) << 17)  |
			((static_cast<Move>(flags)  & 0xFF) << 21);
	}

	constexpr Square GetFromSquare(Move m)    { return (m >>  1) & 0x3F; }
	constexpr Square GetTargetSquare(Move m)  { return (m >>  7) & 0x3F; }
	constexpr Piece   GetMovePiece(Move m)    { return (m >> 13) &  0xF; }
	constexpr Piece   GetPromoPiece(Move m)   { return (m >> 17) &  0xF; }
	constexpr uint8_t GetMoveFlags(Move m)    { return (m >> 21) & 0xFF; }

	Move UCIToMove(const std::string& moveStr);

	std::string MoveToUCI(Move move);

}