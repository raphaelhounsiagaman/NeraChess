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
#include <fstream>

constexpr float INF = 1e9;

NeraChessBot::NeraChessBot(const std::string& modelPath)
 : m_OpeningBook(c_OpeningBookPath)
{
	if (std::filesystem::exists(modelPath))
		std::cout << "Model found\n";
	else
	{
		std::cout << "Model missing (" + modelPath + ")\n";
		return;
	}

	// Initialization of OnnxRuntime
	std::wstring wide(modelPath.begin(), modelPath.end());
	const wchar_t* wmodelPath = wide.c_str();
	m_SessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);;
	m_SessionOptions.SetIntraOpNumThreads(std::thread::hardware_concurrency());;
	m_Session = Ort::Session(m_Env, wmodelPath, m_SessionOptions);

	m_InputVector.resize(1);

	if (!m_OpeningBook)
	{
		std::cout << "Opening book missing (" + c_OpeningBookPath + ")\n";
		m_OpeningBookAvailable = false;
	}
}

NeraChessBot::~NeraChessBot()
{
	m_Session.release();
	m_Env.release();
}

Move NeraChessBot::GetNextMove(const ChessBoard& givenBoard, Timer timer)
{
	m_SearchStartTime = std::chrono::steady_clock::now();
	m_TimeUp = false;

	Move bestMove = 0;
	
	if (m_OpeningBookAvailable)
	{
		bestMove = GetOpeningBookMove(givenBoard);
		if (bestMove != 0)
		{
			std::cout << "Using opening book move: " + MoveUtil::MoveToUCI(bestMove) + "\n";
			return bestMove;
		}
		else
		{
			std::cout << "No opening book move found\n";
			m_OpeningBookAvailable = false;
		}
	}

	ChessBoard board = givenBoard;
	bestMove = IterativeDeepeningSearch(board, 200);
	
	return bestMove;
}

Move NeraChessBot::GetOpeningBookMove(const ChessBoard& board)
{
	std::string fen = board.GetFENString();

	std::string uciMove = "";

	std::string line;
	while (std::getline(m_OpeningBook, line)) {

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

	if (uciMove == "")
		return 0;

	Move bareMove = MoveUtil::UCIToMove(uciMove);

	MoveList<218> legalMoves = board.GetLegalMoves();
	for (Move move : legalMoves)
	{
		if (MoveUtil::GetFromSquare(move) == MoveUtil::GetFromSquare(bareMove) &&
			MoveUtil::GetTargetSquare(move) == MoveUtil::GetTargetSquare(bareMove) &&
			MoveUtil::GetPromoPiece(move) == MoveUtil::GetPromoPiece(bareMove))
		{
			return move;
		}
	}

	return 0;
}

Move NeraChessBot::IterativeDeepeningSearch(ChessBoard& board, int maxDepth)
{
	MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() == 1)
		return legalMoves[0];

	Move bestMove{};

	int depthReached = 0;

	for (uint8_t depth = 1; depth <= maxDepth; depth++)
	{
		if (IsTimeUp()) break;

		m_SearchID++;
		Move move = PVSRoot(board, depth);

		if (m_TimeUp) break;

		bestMove = move;

		std::cout << "Thinking of move " << SquareUtil::SquareAsString(MoveUtil::GetFromSquare(bestMove)) << SquareUtil::SquareAsString(MoveUtil::GetTargetSquare(bestMove)) <<  " at depth " << (int)depth << "\n";

		depthReached = depth;
	}

	std::cout << "Searched Depth " << depthReached << " fully\n";

	return bestMove;
}

Move NeraChessBot::PVSRoot(ChessBoard& board, int depth)
{
	float alpha = -INF;
	float beta = INF;

	float originAlpha = alpha;

	TTEntry* ttProbePtr = m_TranspositionTable.Probe(board.GetZobristKey());
	if (ttProbePtr && ttProbePtr->depth >= depth)
	{
		switch (ttProbePtr->flag)
		{
		case EntryFlag::EXACT:
			return ttProbePtr->bestMove;
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

	MoveList<218> legalMoves = board.GetLegalMoves();

	if (legalMoves.size() == 0)
		return 0;

	if (legalMoves.size() == 1)
		return legalMoves[0];

	SortMoves(board, legalMoves, 0, ttProbePtr ? ttProbePtr->bestMove : 0);

	float bestScore = -INF;
	Move bestMove = legalMoves[0];

	board.MakeMove(legalMoves[0]);
	bestScore = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, 1);
	board.UndoMove(legalMoves[0]);

	alpha = std::max(alpha, bestScore);

	for (size_t moveIndex = 1; moveIndex < legalMoves.size(); moveIndex++)
	{
		Move move = legalMoves[moveIndex];

		board.MakeMove(move);
		float score = std::max(score, -PrincipalVariationSearch(board, -alpha - 1, -alpha, depth - 1, 1));
		if (score > alpha && score < beta)
		{
			score = std::max(score, -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, 1));
		}
		board.UndoMove(move);

		if (m_TimeUp || IsTimeUp())
		{
			m_TimeUp = true;
			return bestMove;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestMove = move;
		}

		alpha = std::max(alpha, bestScore);

		if (alpha >= beta)
		{
			//store_killer_or_history(move);
			m_TranspositionTable.Store(
				board.GetZobristKey(),
				bestScore,
				depth,
				EntryFlag::LOWERBOUND,
				move,
				m_SearchID
			);
			return bestMove;
		}

	}

	// store exact/bound TT entry
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

	return bestMove;
}

float NeraChessBot::PrincipalVariationSearch(ChessBoard& board, float alpha, float beta, int depth, uint8_t ply)
{
	if (m_TimeUp || IsTimeUp())
	{
		m_TimeUp = true;
		return alpha;
	}

	if (depth <= 0)
		//return EvaluateBoard(board);
		return QuiescenceSearch(board, alpha, beta, ply);

	float originAlpha = alpha;

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

	/*
	if (board.MakeNullMove())
	{
		int reduction = NullMoveReduction(depth);

		float score = -PrincipalVariationSearch(board, -beta, -(beta - 1), depth - reduction, ply + 1);

		board.UndoNullMove();
		if (score >= beta)
			return score;
	}
	*/

	if (alpha >= beta)
		return alpha;

	MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() == 0)
		return EvaluateTerminal(board);

	SortMoves(board, legalMoves, ply, ttProbePtr ? ttProbePtr->bestMove : 0);

	float bestScore = -INF;
	Move bestMove = legalMoves[0];

	board.MakeMove(legalMoves[0]);
	bestScore = std::max(bestScore, -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, ply + 1));
	board.UndoMove(legalMoves[0]);

	alpha = std::max(alpha, bestScore);

	if (alpha >= beta)
	{
		m_TranspositionTable.Store(
			board.GetZobristKey(),
			bestScore,
			depth,
			EntryFlag::LOWERBOUND,
			legalMoves[0],
			m_SearchID
		);

		uint8_t flags = MoveUtil::GetMoveFlags(legalMoves[0]);
		if (!(flags & (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION) || board.IsInCheck()))
		{
			if (m_KillerMoves[ply][0] != legalMoves[0])
			{
				m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
				m_KillerMoves[ply][0] = legalMoves[0];
			}
		}
		m_HistoryHeuristic[MoveUtil::GetFromSquare(legalMoves[0])][MoveUtil::GetTargetSquare(legalMoves[0])] += ply * ply;

		return bestScore;
	}

	for (size_t moveIndex = 1; moveIndex < legalMoves.size(); moveIndex++)
	{
		Move move = legalMoves[moveIndex];

		// Late Move Reduction:
		board.MakeMove(move);

		uint8_t flags = MoveUtil::GetMoveFlags(move);
		bool isQuiet = !(flags & (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION)) && !board.IsInCheck();

		uint8_t reduction = 0;


		reduction = LateMoveReduction(depth, ply, (uint8_t)moveIndex);

		float score = -PrincipalVariationSearch(board, -alpha - 1, -alpha, depth - 1 - reduction, ply + 1);
		if (score > alpha && score < beta)
		{
			score = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, ply + 1);
		}

		board.UndoMove(move);

		if (m_TimeUp || IsTimeUp())
		{
			m_TimeUp = true;
			return alpha;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestMove = move;
		}

		alpha = std::max(alpha, score);

		if (alpha >= beta)
		{
			m_TranspositionTable.Store(
				board.GetZobristKey(),
				score,
				depth,
				EntryFlag::LOWERBOUND,
				move,
				m_SearchID
			);

			uint8_t flags = MoveUtil::GetMoveFlags(move);
			if (!(flags & (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION) || board.IsInCheck()))
			{
				if (m_KillerMoves[ply][0] != move)
				{
					m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
					m_KillerMoves[ply][0] = move;
				}
			}
			m_HistoryHeuristic[MoveUtil::GetFromSquare(move)][MoveUtil::GetTargetSquare(move)] += ply * ply;

			return score;
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

float NeraChessBot::QuiescenceSearch(ChessBoard& board, float alpha, float beta, uint8_t ply)
{
	
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

	float static_eval = FastStaticEval(board);

	alpha = std::max(static_eval, alpha);

	if (alpha >= beta)
		return alpha;

	MoveList<218> legalMoves = board.GetLegalMoves();
	MoveList<218> forcingMoves;

	for (Move move : legalMoves)
	{
		uint8_t flags = MoveUtil::GetMoveFlags(move);
		if (flags & (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION))
		{
			forcingMoves.push(move);
		}
	}

	if (forcingMoves.size() == 0)
		return FastStaticEval(board);

	SortMoves(board, forcingMoves, ply, ttEntryPtr ? ttEntryPtr->bestMove : 0);

	float score = -INF;
	for (Move move : forcingMoves)
	{
		board.MakeMove(move);
		score = std::max(score, -QuiescenceSearch(board, -beta, -alpha, ply + 1));
		board.UndoMove(move);

		alpha = std::max(score, alpha);

		if (alpha >= beta)
			return score;
	}

	return score;
}

void NeraChessBot::SortMoves(const ChessBoard& board, MoveList<218>& moves, uint8_t ply, Move ttMove)
{
	static int moveValues[218];

	for (uint32_t i = 0; i < moves.size(); i++) 
	{
		Move move = moves[i];

		int score = 0;

		// TT move first
		if (move == ttMove) {
			moveValues[i] = 10'000'000;
			continue;
		}

		// MVV/LVA scoring
		int attacker = c_PieceValues[MoveUtil::GetMovePiece(move)];
		int victim = c_PieceValues[board.GetPiece(MoveUtil::GetTargetSquare(move))];
		score += victim * 16 - attacker;

		// Promotion bonus
		if (MoveUtil::GetMoveFlags(move) & MoveFlags::IS_PROMOTION)
		{
			score += 1000 + c_PieceValues[MoveUtil::GetPromoPiece(move)];
		}

		// Killer move bonus
		if (move == m_KillerMoves[ply][0]) score += 9'000'000;
		else if (move == m_KillerMoves[ply][1]) score += 8'000'000;

		score += m_HistoryHeuristic[MoveUtil::GetFromSquare(move)][MoveUtil::GetTargetSquare(move)];

		moveValues[i] = score;
	}

	// Simple insertion sort (fast for ~35 moves)
	for (size_t i = 1; i < moves.size(); i++) {
		Move move = moves[i];
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

float NeraChessBot::EvaluateBoard(const ChessBoard& board)
{
	auto cacheIt = s_EvaluationCache.find(board.GetZobristKey());
	if (cacheIt != s_EvaluationCache.end())
		return cacheIt->second;

	m_InputArray = BoardToTensor(board);

	Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
		m_MemoryInfo,
		m_InputArray.data(),
		m_InputArray.size(),
		c_InputShape.data(),
		c_InputShape.size()
	);

	m_InputVector[0] = std::move(inputTensor);

	Ort::RunOptions options;

	std::vector<Ort::Value> outputVector = m_Session.Run(
		options,
		&m_InputName,
		m_InputVector.data(),
		m_InputVector.size(),
		&m_OutputName,
		1
	);

	const float* evalPtr = outputVector.front().GetTensorData<float>();

	s_EvaluationCache[board.GetZobristKey()] = *evalPtr;

	if (board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove))
		return *evalPtr;
	else
		return -*evalPtr;
}

float NeraChessBot::FastStaticEval(const ChessBoard& board)
{
	// Cheap material only + small piece-square bonus if you have it; otherwise return material difference.
	float score = 0;
	for (Square s = 0; s < 64; ++s)
	{
		Piece p = board.GetPiece(s);
		if (p != PieceType::NO_PIECE)
		{
			score += c_PieceValues[p];
		}
	}
	return score;
}

bool NeraChessBot::PositiveSEE(const ChessBoard& board, Move move)
{
	// implement a small SEE routine that returns true if capture is profitable (or equal).
	// Quick cheap version: victim_value - attacker_value >= 0
	int attacker = c_PieceValues[MoveUtil::GetMovePiece(move)];
	int victim = c_PieceValues[board.GetPiece(MoveUtil::GetTargetSquare(move))];
	return (victim - attacker) >= 0;
}

float NeraChessBot::EvaluateTerminal(const ChessBoard& board)
{
	if (board.IsInCheck())
		return board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove) ? -INF + board.GetFullMoveClock() : INF - board.GetFullMoveClock();
	else
		return 0;
}

std::array<float, 19 * 8 * 8> NeraChessBot::BoardToTensor(const ChessBoard& board) const
{
	std::array<float, 19 * 8 * 8> out{};

	const BoardState& boardState = board.GetBoardState();

	for (Square square = 0; square < 64; square++)
	{
		Piece piece = board.GetPiece(square);
		if (piece != PieceType::NO_PIECE)
		{
			uint8_t file = SquareUtil::GetFile(square);
			uint8_t rank = SquareUtil::GetRank(square);

			out[piece * 64 + file * 8 + rank] = 1.0f;
		}
	}

	if (boardState.HasFlag(BoardStateFlags::WhiteToMove))
	{
		float* ptr = &out[12 * 64];
		std::fill(ptr, ptr + 64, 1.0f);
	}

	auto fill_castle = [&](int channel)
		{
			float* ptr = &out[channel * 64];
			std::fill(ptr, ptr + 64, 1.0f);
		};

	if (boardState.HasFlag(BoardStateFlags::CanWhiteCastleKing)) fill_castle(13);
	if (boardState.HasFlag(BoardStateFlags::CanWhiteCastleQueen)) fill_castle(14);
	if (boardState.HasFlag(BoardStateFlags::CanBlackCastleKing)) fill_castle(15);
	if (boardState.HasFlag(BoardStateFlags::CanBlackCastleQueen)) fill_castle(16);

	// en passant
	if (boardState.HasFlag(BoardStateFlags::CanEnPassent))
	{
		uint8_t file = boardState.enPassantFile;
		if (file >= 0 && file <= 7)
		{
			float* ptr = &out[17 * 64 + file * 8];
			std::fill(ptr, ptr + 8, 1.0f); // fill only that file
		}
	}

	// halfmove clock normalized
	float val = static_cast<float>(board.GetHalfMoveClock()) / 50.0f;
	float* ptr = &out[18 * 64];
	std::fill(ptr, ptr + 64, val);

	return out;
}

int NeraChessBot::LateMoveReduction(int depth, uint8_t ply, uint8_t moveIndex) const
{
	if (ply < 3 || moveIndex < 4)
		return 0;
	return (int)std::ceil(0.99 + std::log(ply) * std::log(moveIndex) / 3.14);
}
