#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChessUtil.h"
#include "Piece.h"
#include "Square.h"
#include "Move.h"
#include "Undo.h"
#include "MoveList.h"
#include "BoardState.h"
#include "RepetitionTable.h"
#include "MoveGenerator.h"

using  Bitboard = uint64_t;

enum  GameOverFlags : uint16_t
{
    IS_GAME_CONTINUE = 0,

    IS_GAME_OVER = 1 << 0, // Bit 1

    IS_WHITE_WIN = 1 << 1, // Bit 2
	IS_DRAW = 1 << 2, // Bit 3

    IS_CHECKMATE = 1 << 3, // Bit 4
    IS_RESIGN =  1 << 4, // Bit 5
    IS_TIMEOUT = 1 << 5, // Bit 6

    IS_STALEMATE = 1 << 6, // Bit 7
    IS_REPETITION = 1 << 7, // Bit 8
    IS_50MOVE_RULE = 1 << 8, // Bit 9
    IS_INSUFFICIENT_MATERIAL = 1 << 9, // Bit 10
    IS_AGREE_ON_DRAW = 1 << 10, // Bit 11
};

class ChessBoard
{
public:
	ChessBoard(const std::string& fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	~ChessBoard() = default;

    static void RunPerformanceTest(ChessBoard& board, int calcDepth = 1);

    MoveList<218> GetLegalMoves() const;

    void MakeMove(Move move, bool gameMove = false);
	void UndoMove(Move move);

	bool MakeNullMove(); // TODO: implement correctly
	void UndoNullMove(); // TODO: implement correctly

	const BoardState& GetBoardState() const { return m_BoardState; }

	uint8_t GetHalfMoveClock() const{ return m_HalfMoveClock; }
	uint16_t GetFullMoveClock() const { return m_FullMoves; }

    Piece GetPiece(const uint8_t square) const;
    bool IsInCheck() const;
    uint16_t GetGameOver(bool gameCheck = false) const;

    uint64_t GetZobristKey() const;
    std::string GetFENString() const;

    uint8_t GetError() const { return m_Error; }

    bool operator==(const ChessBoard& other) const;

private:

    static uint64_t PerfTest(int depth, ChessBoard& board);

	static bool InsufficentMaterial(ChessBoard board);

private:

    mutable MoveGenerator m_MoveGenerator;

	mutable MoveList<218> m_LegalMoves{};
	mutable bool m_WasBoardStateChanged = true;

	mutable uint64_t m_ZobristKey = 0;
    mutable bool m_ZobristKeySet = true;

    mutable uint16_t m_GameOverFlags = 0;

    BoardState m_BoardState{};

	RepetitionTable m_RepetitionTable{};
	UndoStack m_UndoStack{};

    std::vector<Move> m_MovesPlayed{};

    uint8_t m_HalfMoveClock = 0;
    uint16_t m_FullMoves = 1;

    static constexpr Bitboard s_SquareBitboard[64] = {
        1ULL <<  0, 1ULL <<  1, 1ULL <<  2, 1ULL <<  3, 1ULL <<  4, 1ULL <<  5, 1ULL <<  6, 1ULL <<  7,
        1ULL <<  8, 1ULL <<  9, 1ULL << 10, 1ULL << 11, 1ULL << 12, 1ULL << 13, 1ULL << 14, 1ULL << 15,
        1ULL << 16, 1ULL << 17, 1ULL << 18, 1ULL << 19, 1ULL << 20, 1ULL << 21, 1ULL << 22, 1ULL << 23,
        1ULL << 24, 1ULL << 25, 1ULL << 26, 1ULL << 27, 1ULL << 28, 1ULL << 29, 1ULL << 30, 1ULL << 31,
        1ULL << 32, 1ULL << 33, 1ULL << 34, 1ULL << 35, 1ULL << 36, 1ULL << 37, 1ULL << 38, 1ULL << 39,
        1ULL << 40, 1ULL << 41, 1ULL << 42, 1ULL << 43, 1ULL << 44, 1ULL << 45, 1ULL << 46, 1ULL << 47,
        1ULL << 48, 1ULL << 49, 1ULL << 50, 1ULL << 51, 1ULL << 52, 1ULL << 53, 1ULL << 54, 1ULL << 55,
        1ULL << 56, 1ULL << 57, 1ULL << 58, 1ULL << 59, 1ULL << 60, 1ULL << 61, 1ULL << 62, 1ULL << 63
    };

    uint8_t m_Error = 0;
};