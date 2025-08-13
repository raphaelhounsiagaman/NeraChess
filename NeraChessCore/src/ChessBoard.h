#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChessBoardFlags.h"

#include "MoveGenerator.h"
#include "BoardState.h"
#include "Piece.h"
#include "Move.h"

typedef uint64_t Bitboard;

class ChessBoard
{
public:
	// Starting FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    
	ChessBoard(const std::string& fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	~ChessBoard() = default;

    std::vector<Move> GetLegalMoves() const;

    void MakeMove(Move move);

    Piece GetPiece(const uint8_t square) const;

    uint8_t GetError() const { return m_Error; }


private:

    mutable MoveGenerator m_MoveGenerator;

    BoardState m_BoardState{};

    // ChessBoardFlags OR'ed together.
    uint16_t m_ChessBoardFlags = 0;

    uint8_t m_HalfMoveClock = 0;
    uint16_t m_FullMoves = 1;

    uint8_t m_Error = 0;

};