#include "FirstNNBot.h"

#include <algorithm>
#include <chrono>

FirstNNBot::FirstNNBot()
{

}

Move FirstNNBot::GetNextMove(const ChessBoard& givenBoard, Timer timer)
{
	ChessBoard board = givenBoard;

	MoveList legalMoves = board.GetLegalMoves();
	if (legalMoves.size() < 2)
		return legalMoves[0];

	SortMoves(board, legalMoves);

	BoardState boardState = board.GetBoardState();
	bool whiteToPlay = boardState.HasFlag(BoardStateFlags::WhiteToMove);
	int8_t colorMultiplier = whiteToPlay ? 1 : -1;

	Move bestMove{};
	double bestEval = whiteToPlay ? -999999 : 999999;

	for (const Move& move : legalMoves)
	{
		board.MakeMove(move);
		double eval = Minimax(board, 2, !whiteToPlay, -99999, 99999);
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
		return 0;

	return bestMove;
}

double FirstNNBot::Minimax(ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta)
{
	uint16_t gameOverFlags = board.GetGameOver();

	if (gameOverFlags & IS_GAME_OVER)
	{
		if (gameOverFlags & IS_CHECKMATE)
			return whiteMaximizingPlayer ? -99999 : 99999;
		else
			return 0;
	}
	else if (depth == 0)
	{
		return EvaluateBoard(board, whiteMaximizingPlayer);
	}

	MoveList legalMoves = board.GetLegalMoves();

	if (whiteMaximizingPlayer)
	{
		double maxEval = -99999;

		for (const Move& move : legalMoves)
		{
			board.MakeMove(move);
			double eval = Minimax(board, depth - 1, false, alpha, beta);
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
		double minEval = 99999;

		for (const Move& move : legalMoves)
		{
			board.MakeMove(move);
			double eval = Minimax(board, depth - 1, true, alpha, beta);
			board.UndoMove(move);

			minEval = std::min(minEval, eval);
			beta = std::min(beta, eval);
			if (beta <= alpha)
				break;

		}

		return minEval;
	}


}

double FirstNNBot::EvaluateBoard(const ChessBoard& board, bool whiteToMove) const
{


	return 0;
}

void FirstNNBot::SortMoves(const ChessBoard& board, MoveList& moves)
{
	std::array<float, 218> moveValues{};

	for (uint8_t i = 0; i < moves.size(); i++)
	{
		float moveScoreGuess = 0;
		Piece movePiece = MoveUtil::GetMovePiece(moves[i]);
		Piece capturePiece = board.GetPiece(MoveUtil::GetTargetSquare(moves[i]));

		if (capturePiece != PieceType::NO_PIECE)
			moveScoreGuess += 10 * m_PieceValues[capturePiece] - m_PieceValues[movePiece];
		if (MoveUtil::GetMoveFlags(moves[i]) & MoveFlags::IS_PROMOTION)
			moveScoreGuess += m_PieceValues[MoveUtil::GetPromoPiece(moves[i])];
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

	for (int i = 0; i < moves.size(); i++)
	{
		moveValuePairs.emplace_back(moves[i], moveValues[i]);
	}

	std::sort(moveValuePairs.begin(), moveValuePairs.end(),
		[](const MoveValuePair& a, const MoveValuePair& b) {
			return a.value > b.value;
		});

	for (int i = 0; i < moves.size(); i++)
	{
		moves[i] = moveValuePairs[i].move;
	}

	return;
}
