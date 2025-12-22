#include "NeraChessBot.h"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <array>
#include <string>
#include <stdexcept>
#include <cctype>
#include <iostream>
#include <cmath>
#include <thread>
#include <future>
#include <fstream>

constexpr float INF = 1e2;

NeraChessBot::NeraChessBot(const std::string& modelPath)
 : m_OpeningBook(c_OpeningBookPath), m_NeuralNetwork(modelPath)
{
	if (!m_OpeningBook)
	{
		std::cout << "Opening book missing (" + c_OpeningBookPath + ")\n";
		m_OpeningBookAvailable = false;
	}
}

ChessCore::Move NeraChessBot::GetNextMove(const ChessCore::ChessBoard& givenBoard,const ChessCore::Clock& timer)
{
	m_SearchStartTime = std::chrono::steady_clock::now();
	m_TimeUp = false;

	ChessCore::Move bestMove = 0;
	
	if (m_OpeningBookAvailable)
	{
		bestMove = GetOpeningBookMove(givenBoard);
		if (bestMove != 0)
		{
			std::cout << "Using opening book move: " + bestMove.ToUCI() + "\n";
			return bestMove;
		}
		else
		{
			std::cout << "No opening book move found\n";
			m_OpeningBookAvailable = false;
		}
	}

	ChessCore::ChessBoard board = givenBoard;
	bestMove = IterativeDeepeningSearch(board, 200);
	
	if (m_StopSearching)
	{
		m_StopSearching = false;
		return givenBoard.GetLegalMoves()[0];
	}

	return bestMove;
}

ChessCore::Move NeraChessBot::GetOpeningBookMove(const ChessCore::ChessBoard& board)
{
	std::string fen = board.GetFENString();

	std::string uciMove = "";

	std::string line;
	while (std::getline(m_OpeningBook, line))
	{
		if (m_StopSearching)
			return 0;
		size_t position = line.find(',');
		if (position == std::string::npos) continue;

		// Extract fen
		// Avoid extra allocations: compare directly
		if (line.compare(0, position, fen) == 0) {
			// Found → extract move
			uciMove = line.substr(position + 1);
			break;
		}
	}

	m_OpeningBook.clear();
	m_OpeningBook.seekg(0);

	if (uciMove == "")
		return 0;

	ChessCore::Move bareMove(uciMove);

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
	for (ChessCore::Move move : legalMoves)
	{
		if (move.GetStartSquare() == bareMove.GetStartSquare() &&
			move.GetTargetSquare() == bareMove.GetTargetSquare() &&
			move.GetPromoPiece() == bareMove.GetPromoPiece())
		{
			return move;
		}
	}

	return 0;
}

ChessCore::Move NeraChessBot::IterativeDeepeningSearch(ChessCore::ChessBoard& board, int maxDepth)
{
	if (m_StopSearching)
		return 0;
	m_NodesSearched = 0;
	m_NodesEvaluated = 0;
	m_QuiescenceNodesSearched = 0;

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() == 1)
		return legalMoves[0];

	ChessCore::Move bestMove = 0;

	uint8_t depthReached = 0;

	for (uint8_t depth = 1; depth <= maxDepth; depth++)
	{
		m_SearchID++;
		ChessCore::Move move = PVSRoot(board, depth);

		if (m_TimeUp || m_StopSearching) break;

		bestMove = move;
		depthReached = depth;

		std::cout << "Thinking of move " <<
			bestMove.GetStartSquare().ToString() <<
			bestMove.GetTargetSquare().ToString() <<
			" at depth " << (int)depthReached << "\n";

		std::cout << "Nodes At depth: ";
		for (uint8_t d = 0; d <= depthReached; d++)
		{
			std::cout << (int)d << ": " << m_NodesAtDepth[d] << ", ";
			m_NodesAtDepth[d] = 0;
		}
		std::cout << "\n";

		//std::cout << "Nodes searched: " << m_NodesSearched << "\n";
		//std::cout << "Nodes evaluated: " << m_NodesEvaluated << "\n";
		//std::cout << "Quiescence nodes searched: " << m_QuiescenceNodesSearched << "\n";
		
	}

	std::cout << "Searched Depth " << (int)depthReached << " fully\n";

	return bestMove;
}

ChessCore::Move NeraChessBot::PVSRoot(ChessCore::ChessBoard& board, int depth)
{
	if (m_StopSearching)
		return 0;

	TTEntry* ttProbePtr = m_TranspositionTable.Probe(board.GetZobristKey());
	if (ttProbePtr && ttProbePtr->depth >= depth)
	{
		switch (ttProbePtr->flag)
		{
		case EntryFlag::EXACT:
			return ttProbePtr->bestMove;
		}
	}

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();

	SortMoves(board, legalMoves, 0, ttProbePtr ? ttProbePtr->bestMove : ChessCore::Move(0));

	float bestScore = -INF;
	ChessCore::Move bestMove = legalMoves[0];

	board.MakeMove(bestMove);
	bestScore = -PrincipalVariationSearch(board, -INF, INF, depth - 1, 1);
	board.UndoMove(bestMove);

	std::cout << "Assuming best move is: " << bestMove.ToUCI() << " with score " << (float)bestScore << "\n";

	for (ChessCore::Move move : legalMoves)
	{
		if (move == legalMoves[0])
			continue;

		board.MakeMove(move);
		float score = -PrincipalVariationSearch(board, -INF, INF, depth - 1, 1);
		board.UndoMove(move);

		if (score > bestScore)
		{
			bestScore = score;
			bestMove = move;

			std::cout << "New best move: " << bestMove.ToUCI() << " with score " << (float)bestScore << "\n";
		}

		if (IsTimeUp())
		{
			return bestMove;
		}

	}

	return bestMove;
}

float NeraChessBot::PrincipalVariationSearch(ChessCore::ChessBoard& board, float alpha, float beta, int depth, uint8_t ply)
{
	if (m_StopSearching)
		return 0;

	m_NodesSearched++;

	if (IsTimeUp())
	{
		return alpha;
	}

	// TODO: Null Move Pruning

	if (depth <= 0)
		return QuiescenceSearch(board, alpha, beta, ply);

	m_NodesAtDepth[ply]++;
	TTEntry* ttProbePtr = m_TranspositionTable.Probe(board.GetZobristKey());
	if (ttProbePtr && ttProbePtr->depth >= depth)
	{
		switch (ttProbePtr->flag)
		{
		case EntryFlag::EXACT:
			return ttProbePtr->value;
		case EntryFlag::LOWERBOUND:
			if (ttProbePtr->value > alpha)
				alpha = ttProbePtr->value;
			break;
		case EntryFlag::UPPERBOUND:
			if (ttProbePtr->value < beta)
				beta = ttProbePtr->value;
			break;
		}
	}

	float originAlpha = alpha;

	if (alpha >= beta)
		return beta;

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() == 0)
		return EvaluateTerminal(board);

	for (ChessCore::Move move : legalMoves)
	{
		board.MakeMove(move);
		m_NeuralNetwork.QueuePosition(board);
		board.UndoMove(move);
	}

	SortMoves(board, legalMoves, ply, ttProbePtr ? ttProbePtr->bestMove : ChessCore::Move(0));

	float bestScore = -INF;
	ChessCore::Move bestMove = legalMoves[0];

	board.MakeMove(bestMove);
	bestScore = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, ply + 1);
	board.UndoMove(bestMove);

	alpha = std::max(alpha, bestScore);

	if (alpha >= beta)
	{
		m_TranspositionTable.Store(
			board.GetZobristKey(),
			beta,
			depth,
			EntryFlag::LOWERBOUND,
			bestMove,
			m_SearchID
		);

		bool isQuiet = !(bestMove.GetMoveFlags() & (ChessCore::MoveFlags::IS_CAPTURE | ChessCore::MoveFlags::IS_PROMOTION)) && !board.IsInCheck();
		if (isQuiet)
		{
			if (m_KillerMoves[ply][0] != bestMove && m_KillerMoves[ply][1] != bestMove)
			{
				m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
				m_KillerMoves[ply][0] = bestMove;
			}
		}
		m_HistoryHeuristic[bestMove.GetStartSquare()][bestMove.GetTargetSquare()] += ply * ply;

		return beta;
	}

	for (size_t moveIndex = 1; moveIndex < legalMoves.size(); moveIndex++)
	{
		ChessCore::Move move = legalMoves[moveIndex];

		board.MakeMove(move);

		bool isQuiet = !(move.GetMoveFlags() & (ChessCore::MoveFlags::IS_CAPTURE | ChessCore::MoveFlags::IS_PROMOTION)) && !board.IsInCheck();

		uint8_t reduction = 0;

		if (isQuiet)
		{
			// Futility Pruning

			float futilityMargin = ply;

			if (-EvaluateBoard(board) + futilityMargin < alpha)
			{
				board.UndoMove(move);
				continue;
			}

			reduction = LateMoveReduction(depth, ply, (uint8_t)moveIndex);
		}

		float score = -PrincipalVariationSearch(board, -alpha - 1, -alpha, depth - 1 - reduction, ply + 1);
		if (score > alpha) // && score < beta
		{
			score = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, ply + 1);
		}

		board.UndoMove(move);

		if (IsTimeUp())
		{
			return alpha;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestMove = move;
			alpha = std::max(alpha, bestScore);
		}

		if (alpha >= beta)
		{
			m_TranspositionTable.Store(
				board.GetZobristKey(),
				beta,
				depth,
				EntryFlag::LOWERBOUND,
				bestMove,
				m_SearchID
			);

			bool isQuiet = !(move.GetMoveFlags() & (ChessCore::MoveFlags::IS_CAPTURE | ChessCore::MoveFlags::IS_PROMOTION)) && !board.IsInCheck();

			if (isQuiet)
			{
				if (m_KillerMoves[ply][0] != bestMove && m_KillerMoves[ply][1] != bestMove)
				{
					m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
					m_KillerMoves[ply][0] = bestMove;
				}
			}
			m_HistoryHeuristic[bestMove.GetStartSquare()][bestMove.GetTargetSquare()] += ply * ply;

			return beta;
		}

	}

	EntryFlag flag;
	if (bestScore <= originAlpha)
		flag = EntryFlag::UPPERBOUND;
	else
		flag = EntryFlag::EXACT;

	m_TranspositionTable.Store(
		board.GetZobristKey(),
		bestScore,
		depth,
		flag,
		bestMove,
		m_SearchID);

	return bestScore;
}

float NeraChessBot::QuiescenceSearch(ChessCore::ChessBoard& board, float alpha, float beta, uint8_t ply)
{
	if (m_StopSearching)
		return 0;

	m_NodesAtDepth[ply]++;
	m_NodesSearched++;
	m_QuiescenceNodesSearched++;

	TTEntry* ttEntryPtr = m_TranspositionTable.Probe(board.GetZobristKey());
	if (ttEntryPtr)
	{
		switch (ttEntryPtr->flag)
		{
		case EntryFlag::EXACT:
			return ttEntryPtr->value;
		case EntryFlag::LOWERBOUND:
			if (ttEntryPtr->value > alpha)
				alpha = ttEntryPtr->value;
			break;
		case EntryFlag::UPPERBOUND:
			if (ttEntryPtr->value < beta)
				beta = ttEntryPtr->value;
			break;
		}
	}
	
	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
	ChessCore::MoveList<218> forcingMoves;

	for (ChessCore::Move move : legalMoves)
	{
		uint8_t flags = move.GetMoveFlags();
		if (flags & (ChessCore::MoveFlags::IS_CAPTURE | ChessCore::MoveFlags::IS_PROMOTION))
		{
			forcingMoves.push(move);
		}
	}

	if (forcingMoves.size() == 0)
		return EvaluateBoard(board);

	for (ChessCore::Move move : forcingMoves)
	{
		board.MakeMove(move);
		m_NeuralNetwork.QueuePosition(board);
		board.UndoMove(move);
	}

	if (forcingMoves.size() > 4)
		SortMoves(board, forcingMoves, ply, ttEntryPtr ? ttEntryPtr->bestMove : ChessCore::Move(0));

	alpha = std::max(EvaluateBoard(board), alpha);

	if (alpha >= beta)
		return alpha;

	float score = -INF;
	for (ChessCore::Move move : forcingMoves)
	{
		board.MakeMove(move);
		score = std::max(score, -QuiescenceSearch(board, -beta, -alpha, ply + 1));
		board.UndoMove(move);

		alpha = std::max(score, alpha);

		if (alpha >= beta)
			return alpha;
	}

	return alpha;
}

void NeraChessBot::SortMoves(const ChessCore::ChessBoard& board, ChessCore::MoveList<218>& moves, uint8_t ply, ChessCore::Move ttMove)
{
	static int moveValues[218];

	for (uint8_t i = 0; i < moves.size(); i++)
	{
		ChessCore::Move move = moves[i];

		int score = 0;

		// TT move first
		if (move == ttMove) {
			moveValues[i] = 10'000'000;
			continue;
		}

		// MVV/LVA scoring
		if (move.GetMoveFlags() & ChessCore::MoveFlags::IS_CAPTURE)
		{
			int attacker = (int)c_PieceValues[move.GetMovePiece()];
			int victim = (int)c_PieceValues[board.GetPiece(move.GetTargetSquare())];
			score += (victim - attacker) + 8'000'000;
		}
		// Promotion bonus
		if (move.GetMoveFlags() & ChessCore::MoveFlags::IS_PROMOTION)
		{
			score += 1000 + (int)c_PieceValues[move.GetPromoPiece()];
		}

		// Killer move bonus
		if (move == m_KillerMoves[ply][0]) score += 7'000'000;
		else if (move == m_KillerMoves[ply][1]) score += 6'000'000;

		score += m_HistoryHeuristic[move.GetStartSquare()][move.GetTargetSquare()];

		moveValues[i] = score;
	}

	// Simple insertion sort (fast for ~35 moves)
	for (size_t i = 1; i < moves.size(); i++) {
		ChessCore::Move move = moves[i];
		int s = moveValues[i];
		size_t j = i;
		while (j > 0 && moveValues[j - 1] < s) {
			moveValues[j] = moveValues[j - 1];
			moves[j] = moves[j - 1];
			j--;
		}
		moveValues[j] = s;
		moves[j] = move;
	}
}

float NeraChessBot::EvaluateBoard(const ChessCore::ChessBoard& board)
{
	return m_NeuralNetwork.GetEvaluation(board);
}

float NeraChessBot::FastStaticEval(const ChessCore::ChessBoard& board)
{
	// Cheap material only + small piece-square bonus if you have it; otherwise return material difference.
	float score = 0;
	for (ChessCore::Square s = 0; s < 64; s++)
	{
		ChessCore::Piece p = board.GetPiece(s);
		if (p != ChessCore::PieceType::NO_PIECE)
		{
			score += c_PieceValues[p];
		}
	}
	return score * float(board.GetBoardState().HasFlag(ChessCore::BoardStateFlags::WhiteToMove) ? 1.f : -1.f);
}

bool NeraChessBot::PositiveSEE(const ChessCore::ChessBoard& board, ChessCore::Move move)
{
	// implement a small SEE routine that returns true if capture is profitable (or equal).
	// Quick cheap version: victim_value - attacker_value >= 0
	int attacker = (int)c_PieceValues[move.GetMovePiece()];
	int victim = (int)c_PieceValues[board.GetPiece(move.GetTargetSquare())];
	return (victim - attacker) >= 0;
}

float NeraChessBot::EvaluateTerminal(const ChessCore::ChessBoard& board)
{
	if (board.IsInCheck())
		return -INF + board.GetFullMoveClock();
	else
		return 0;
}



int NeraChessBot::LateMoveReduction(int depth, uint8_t ply, uint8_t moveIndex) const
{
	if (ply < 3 || moveIndex < 2)
		return 0;
	return 1 + moveIndex / 4;
}

bool NeraChessBot::IsTimeUp()
{
	if (m_TimeUp)
		return true;
	m_TimeUp = (std::chrono::steady_clock::now() - m_SearchStartTime) >= m_TimeLimitS;
	return m_TimeUp;
}
