#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Util.h"
#include "Piece.h"
#include "Square.h"
#include "Move.h"
#include "Undo.h"
#include "MoveList.h"
#include "BoardState.h"
#include "MoveGenerator.h"

typedef uint64_t Bitboard;

enum  class GameOverFlags : uint16_t
{
    IS_GAME_CONTINUE = 0,

    IS_GAME_OVER = 1, // Bit 1

    IS_WHITE_WIN = 2, // Bit 2

    IS_CHECKMATE = 4, // Bit 3
    IS_RESIGN = 8, // Bit 4
    IS_TIMEOUT = 16, // Bit 5

    IS_STALEMATE = 32, // Bit 6
    IS_REPETITION = 64, // Bit 7
    IS_50MOVE_RULE = 128, // Bit 8
    IS_INSUFFICIENT_MATERIAL = 256, // Bit 9
    IS_AGREE_ON_DRAW = 512, // Bit 10
};

class ChessBoard
{
public:
	// Starting FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    
	ChessBoard(const std::string& fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	~ChessBoard() = default;

    bool operator==(const ChessBoard& other) const;

    MoveList GetLegalMoves() const;

    void MakeMove(Move move);
	void UndoMove(Move move);

    Piece GetPiece(const uint8_t square) const;
	uint16_t GetGameOverFlags() const { return m_GameOverFlags; }

    uint8_t GetError() const { return m_Error; }

    static void RunPerformanceTest(ChessBoard board = {}, int calcDepth = 1);

private:

    static uint64_t PerfTest(int depth, ChessBoard board);

    PieceType GetPiece(uint8_t square);

	static bool InsufficentMaterial(ChessBoard board);

    mutable MoveGenerator m_MoveGenerator;

    BoardState m_BoardState{};

	UndoStack m_UndoStack{};

    std::vector<Move> m_MovesPlayed{};

    // GameOverFlags OR'ed together.
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