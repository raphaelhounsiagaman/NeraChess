#include "FirstNNBot.h"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <array>
#include <string>
#include <stdexcept>
#include <cctype>
#include <iostream>
#include <cmath>

FirstNNBot::FirstNNBot(const std::string& modelPath)
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
	m_Session = Ort::Session(m_Env, wmodelPath, m_SessionOptions);

	m_InputVector.resize(1);

}

FirstNNBot::~FirstNNBot()
{
	m_Session.release();
	m_Env.release();
}

Move FirstNNBot::GetNextMove(const ChessBoard& givenBoard, Timer timer)
{
	ChessBoard board = givenBoard;
	
	MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() == 1)
		return legalMoves[0];

	SortMoves(board, legalMoves);

	const BoardState& boardState = board.GetBoardState();
	bool whiteToPlay = boardState.HasFlag(BoardStateFlags::WhiteToMove);
	int8_t colorMultiplier = whiteToPlay ? 1 : -1;

	Move bestMove{};
	float bestEval = whiteToPlay ? -999999.f : 999999.f;

	for (const Move& move : legalMoves)
	{
		board.MakeMove(move);
		float eval = MinimaxSearch(board, 2, !whiteToPlay, -99999, 99999);
		board.UndoMove(move);

		if ((whiteToPlay && (eval > bestEval)) || (!whiteToPlay && (eval < bestEval)))
		{
			bestEval = eval;
			bestMove = move;
		}

		if (bestEval == 9999 * colorMultiplier)
			break;
	}

	if (bestMove == 0)
		throw;

	return bestMove;
}

float FirstNNBot::MinimaxSearch(ChessBoard& board, int depth, bool whiteMaximizingPlayer, float alpha, float beta)
{
	uint16_t gameOverFlags = board.GetGameOver();

	if (gameOverFlags & IS_GAME_OVER)
	{
		if (gameOverFlags & IS_CHECKMATE)
			return whiteMaximizingPlayer ? -99999.f : 99999.f;
		else
			return 0;
	}
	else if (depth == 0)
	{
		return EvaluateBoard(board, whiteMaximizingPlayer);
	}

	MoveList<218> legalMoves = board.GetLegalMoves();

	if (whiteMaximizingPlayer)
	{
		float maxEval = -99999;

		for (const Move& move : legalMoves)
		{
			board.MakeMove(move);
			float eval = MinimaxSearch(board, depth - 1, false, alpha, beta);
			board.UndoMove(move);

			maxEval = std::max(maxEval, eval);
			alpha = std::max(alpha, eval);
			if (beta <= alpha)
				break;
		}

		return maxEval;
	}
	else
	{
		float minEval = 99999;

		for (const Move& move : legalMoves)
		{
			board.MakeMove(move);
			float eval = MinimaxSearch(board, depth - 1, true, alpha, beta);
			board.UndoMove(move);

			minEval = std::min(minEval, eval);
			beta = std::min(beta, eval);
			if (beta <= alpha)
				break;
		}

		return minEval;
	}

}

void FirstNNBot::SortMoves(const ChessBoard& board, MoveList<218>& moves)
{
	std::array<float, 218> moveValues{};

	for (uint8_t i = 0; i < moves.size(); i++)
	{
		float moveScoreGuess = 0;
		Piece movePiece = MoveUtil::GetMovePiece(moves[i]);
		Piece capturePiece = board.GetPiece(MoveUtil::GetTargetSquare(moves[i]));

		if (capturePiece != PieceType::NO_PIECE)
			moveScoreGuess += 10 * c_PieceValues[capturePiece] - c_PieceValues[movePiece];
		if (MoveUtil::GetMoveFlags(moves[i]) & MoveFlags::IS_PROMOTION)
			moveScoreGuess += c_PieceValues[MoveUtil::GetPromoPiece(moves[i])];
		//if (board.SquareIsAttackedByOpponent(moves[i].TargetSquare))
		//	moveScoreGuess -= (float)(piece_values[move_piece_type]);

		if (MoveUtil::GetMoveFlags(moves[i]) & MoveFlags::IS_PROMOTION)
			moveScoreGuess += 10;

		moveValues[i] = moveScoreGuess;
	}

	struct MoveValuePair
	{
		Move move;
		float value;
	};

	std::vector<MoveValuePair> moveValuePairs;
	moveValuePairs.reserve(moves.size());

	for (uint32_t i = 0; i < moves.size(); i++)
	{
		moveValuePairs.emplace_back(moves[i], moveValues[i]);
	}

	std::sort(moveValuePairs.begin(), moveValuePairs.end(),
		[](const MoveValuePair& a, const MoveValuePair& b) {
			return a.value > b.value;
		});

	for (uint32_t i = 0; i < moves.size(); i++)
	{
		moves[i] = moveValuePairs[i].move;
	}

	return;
}

float FirstNNBot::EvaluateBoard(const ChessBoard& board, bool whiteToMove)
{
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

	return *evalPtr;
}

std::array<float, 19 * 8 * 8> FirstNNBot::BoardToTensor(const ChessBoard& board) const
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
