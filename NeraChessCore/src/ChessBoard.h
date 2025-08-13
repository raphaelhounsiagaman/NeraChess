#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GameOverFlags.h"

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
	void UndoMove(Move move);

    Piece GetPiece(const uint8_t square) const;

    uint8_t GetError() const { return m_Error; }

    static void RunPerformanceTest(ChessBoard board = {}, int calcDepth = 1);

private:

    static uint64_t PerfTest(int depth, ChessBoard board);

	static bool InsufficentMaterial(ChessBoard board);

    mutable MoveGenerator m_MoveGenerator;

    BoardState m_BoardState{};

    std::vector<Move> m_MovesPlayed{};

    // ChessBoardFlags OR'ed together.
    uint16_t m_GameOverFlags = 0;

    uint8_t m_HalfMoveClock = 0;
    uint16_t m_FullMoves = 1;
    
	uint16_t m_FirstWhiteKingMove = 0;
	uint16_t m_FirstBlackKingMove = 0;

    uint16_t m_FirstWhiteKingRookMove = 0;
    uint16_t m_FirstWhiteQueenRookMove = 0;
    uint16_t m_FirstBlackKingRookMove = 0;
    uint16_t m_FirstBlackQueenRookMove = 0;

    uint8_t m_Error = 0;

};