#pragma once

#include "../ChessPlayer.h"
#include "Timer.h"

#include <unordered_map>

enum NodeType : uint8_t
{
	Exact,
	LowerBound,  
	UpperBound
};

struct TranspositionTableEntry
{
	double evaluation = 0;

	uint8_t depth = 0;
	NodeType type = NodeType::Exact;

		//public Move BestMove { get; set; }  // Best move found from this position
};

struct TranspositionTable
{
	std::unordered_map<uint64_t, TranspositionTableEntry> table{};

	bool TryGetValue(uint64_t zobristKey, TranspositionTableEntry& entry)
	{
		auto it = table.find(zobristKey);
		if (it != table.end())
		{
			entry = it->second;
			return true;
		}
		return false;
	}

	void StoreEntry(uint64_t zobristKey, const TranspositionTableEntry& entry)
	{
		table[zobristKey] = entry;
	}

};

class MyBotOld : public ChessPlayer
{
public:
	
	Move GetNextMove(const ChessBoard& givenBoard, Timer timer) override;

private:

	double Minimax(ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta);

	double EvaluateBoard(const BoardState& board, bool whiteToMove) const;

	static void SortMoves(const ChessBoard& board, MoveList& moves);

private:

	TranspositionTable m_TranspositionTable{};

	static constexpr float m_PieceValues[12] = {
		10.0f, // WHITE_PAWN
		30.0f, // WHITE_KNIGHT
		32.0f, // WHITE_BISHOP
		50.0f, // WHITE_ROOK
		90.0f, // WHITE_QUEEN
		10000.0f, // WHITE_KING
		-10.0f, // BLACK_PAWN
		-30.0f, // BLACK_KNIGHT
		-32.0f, // BLACK_BISHOP
		-50.0f, // BLACK_ROOK
		-90.0f, // BLACK_QUEEN
		-10000.0f // BLACK_KING
	};

	static constexpr float m_PawnPositionValues[64] = {
		 0,  0,  0,  0,  0,  0,  0,  0,
		50, 50, 50, 50, 50, 50, 50, 50,
		10, 10, 20, 30, 30, 20, 10, 10,
		 5,  5, 10, 25, 25, 10,  5,  5,
		 0,  0,  0, 20, 20,  0,  0,  0,
		 5, -5,-10,  0,  0,-10, -5,  5,
		 5, 10, 10,-20,-20, 10, 10,  5,
		 0,  0,  0,  0,  0,  0,  0,  0
	};

	static constexpr float m_KnightPositionValues[64] = {
		-50,-40,-30,-30,-30,-30,-40,-50,
		-40,-20,  0,  0,  0,  0,-20,-40,
		-30,  0, 10, 15, 15, 10,  0,-30,
		-30,  5, 15, 20, 20, 15,  5,-30,
		-30,  0, 15, 20, 20, 15,  0,-30,
		-30,  5, 10, 15, 15, 10,  5,-30,
		-40,-20,  0,  5,  5,  0,-20,-40,
		-50,-40,-30,-30,-30,-30,-40,-50,
	};

	static constexpr float m_BishopPositionValues[64] = {
		-20,-10,-10,-10,-10,-10,-10,-20,
		-10,  0,  0,  0,  0,  0,  0,-10,
		-10,  0,  5, 10, 10,  5,  0,-10,
		-10,  5,  5, 10, 10,  5,  5,-10,
		-10,  0, 10, 10, 10, 10,  0,-10,
		-10, 10, 10, 10, 10, 10, 10,-10,
		-10,  5,  0,  0,  0,  0,  5,-10,
		-20,-10,-10,-10,-10,-10,-10,-20,
	};

	static constexpr float m_RookPositionValues[64] = {
		 0,  0,  0,  0,  0,  0,  0,  0,
		  5, 10, 10, 10, 10, 10, 10,  5,
		 -5,  0,  0,  0,  0,  0,  0, -5,
		 -5,  0,  0,  0,  0,  0,  0, -5,
		 -5,  0,  0,  0,  0,  0,  0, -5,
		 -5,  0,  0,  0,  0,  0,  0, -5,
		 -5,  0,  0,  0,  0,  0,  0, -5,
		  0,  0,  0,  5,  5,  0,  0,  0
	};

	static constexpr float m_QueenPositionValues[64] = {
		 -20,-10,-10, -5, -5,-10,-10,-20,
		-10,  0,  0,  0,  0,  0,  0,-10,
		-10,  0,  5,  5,  5,  5,  0,-10,
		 -5,  0,  5,  5,  5,  5,  0, -5,
		  0,  0,  5,  5,  5,  5,  0, -5,
		-10,  5,  5,  5,  5,  5,  0,-10,
		-10,  0,  5,  0,  0,  0,  0,-10,
		-20,-10,-10, -5, -5,-10,-10,-20
	};

	static constexpr float m_KingPositionMiddleGameValues[64] = {
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-20,-30,-30,-40,-40,-30,-30,-20,
		-10,-20,-20,-20,-20,-20,-20,-10,
		 20, 20,  0,  0,  0,  0, 20, 20,
		 20, 30, 10,  0,  0, 10, 30, 20
	};

	static constexpr float m_KingPositionEndGameValues[64] = {
		-50,-40,-30,-20,-20,-30,-40,-50,
		-30,-20,-10,  0,  0,-10,-20,-30,
		-30,-10, 20, 30, 30, 20,-10,-30,
		-30,-10, 30, 40, 40, 30,-10,-30,
		-30,-10, 30, 40, 40, 30,-10,-30,
		-30,-10, 20, 30, 30, 20,-10,-30,
		-30,-30,  0,  0,  0,  0,-30,-30,
		-50,-30,-30,-30,-30,-30,-30,-50
	};

};
