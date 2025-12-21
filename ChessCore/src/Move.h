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
* | 1–6   | start square    |
* | 7–12  | target square   |
* | 13–16 | move piece      |
* | 17–20 | promotion piece |
* | 21–28 | move flags      |
* | 29–31 | unused		    |
* 
*/

namespace ChessCore
{

	enum MoveFlags : uint8_t
	{
		NO_MOVE = 0,

		IS_CAPTURE = 1 << 0, // Bit 1
		IS_EN_PASSANT = 1 << 1, // Bit 2
		IS_PROMOTION = 1 << 2, // Bit 3
		IS_CASTLES = 1 << 3, // Bit 4
		PAWN_TWO_UP = 1 << 4, // Bit 5
	};

	struct Move
	{
		Move() = default;
		Move(uint32_t m) : move(m) {};
		Move(Square startSquare, Square targetSquare, Piece movePiece = 0, Piece promoPiece = 0, uint8_t flags = 0);
		Move(const std::string& moveStr);

		uint32_t move{ 0 };

		operator uint32_t() const { return move; }
		operator std::string() const { return ToUCI(); }

		Square GetStartSquare() const { return (move >> 1) & 0x3F; }
		Square GetTargetSquare() const { return (move >> 7) & 0x3F; }
		Piece   GetMovePiece() const { return (move >> 13) & 0xF; }
		Piece   GetPromoPiece() const { return (move >> 17) & 0xF; }
		uint8_t GetMoveFlags() const { return (move >> 21) & 0xFF; }

		std::string ToUCI() const;

	};


} // namespace ChessCore