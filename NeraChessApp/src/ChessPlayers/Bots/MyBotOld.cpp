#include "MyBotOld.h"

#include <algorithm>
#include <chrono>

Move MyBotOld::GetNextMove(const ChessBoard& givenBoard, Timer timer)
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
		double eval = Minimax(board, 4, !whiteToPlay, -99999, 99999);
		board.UndoMove(move);

		if ((whiteToPlay && (eval > bestEval)) || (!whiteToPlay && (eval < bestEval)))
		{
			bestEval = eval;
			bestMove = move;
		}
	}

	if (bestMove == 0)
		return 0;

    return bestMove;
}

double MyBotOld::Minimax(ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta)
{
	uint16_t gameOverFlags = board.GetGameOver();

	double originAlpha = alpha;
	double originBeta = beta;

	if (gameOverFlags & IS_GAME_OVER)
	{
		if (gameOverFlags & IS_CHECKMATE)
			return whiteMaximizingPlayer ? -99999 : 99999;
		else
			return 0;
	}
	else if (depth == 0)
	{
		return EvaluateBoard(board.GetBoardState(), whiteMaximizingPlayer);
	}

	uint64_t zobristKey = board.GetZobristKey();
	TranspositionTableEntry entry;

	if (m_TranspositionTable.TryGetValue(zobristKey, entry))
	{
		// If the stored depth is greater or equal to the current depth, use the stored evaluation
		if (entry.depth >= depth)
		{
			if (entry.type == NodeType::Exact)
			{
				return entry.evaluation;
			}
			else if (entry.type == NodeType::LowerBound && entry.evaluation > alpha)
			{
				alpha = entry.evaluation;
			}
			else if (entry.type == NodeType::UpperBound && entry.evaluation < beta)
			{
				beta = entry.evaluation;
			}

			if (alpha >= beta)
			{
				return entry.evaluation;
			}
		}
	}

	MoveList legalMoves = board.GetLegalMoves();


	double bestEval = whiteMaximizingPlayer ? -99999 : 99999;

	for (const Move& move : legalMoves)
	{
		board.MakeMove(move);
		double eval = Minimax(board, depth - 1, !whiteMaximizingPlayer, alpha, beta);
		board.UndoMove(move);

		bestEval = whiteMaximizingPlayer ? std::max(bestEval, eval) : std::min(bestEval, eval);

		if (whiteMaximizingPlayer)
			alpha = std::max(alpha, eval);
		else
			beta = std::min(beta, eval);

		if (beta <= alpha)
			break;
	}

	NodeType type;
	if (bestEval <= originAlpha)
	{
		type = NodeType::UpperBound;
	}
	else if (bestEval >= originBeta)
	{
		type = NodeType::LowerBound;
	}
	else
	{
		type = NodeType::Exact;
	}

	// Store the result in the transposition table
	m_TranspositionTable.StoreEntry(zobristKey, TranspositionTableEntry
		(
			bestEval,
			depth,
			type
		));

	return bestEval;
}

double MyBotOld::EvaluateBoard(const BoardState& boardState, bool whiteToMove) const
{
	double evaluation = 0.0;

	float pieceValueMultiplier = 5.f;

	Bitboard importantPieces =
		boardState.pieceBitboards[WHITE_PAWN]   |
		boardState.pieceBitboards[WHITE_KNIGHT] |
		boardState.pieceBitboards[WHITE_BISHOP] |
		boardState.pieceBitboards[WHITE_ROOK]   |
		boardState.pieceBitboards[WHITE_QUEEN]  |
		boardState.pieceBitboards[WHITE_KING]   |
		boardState.pieceBitboards[BLACK_PAWN]   |
		boardState.pieceBitboards[BLACK_KNIGHT] |
		boardState.pieceBitboards[BLACK_BISHOP] |
		boardState.pieceBitboards[BLACK_ROOK]   |
		boardState.pieceBitboards[BLACK_QUEEN]  |
		boardState.pieceBitboards[BLACK_KING];

	float endGame = 1.0f - (float)(BitUtil::PopCnt(importantPieces) / 17);

	for (Piece piece = 0; piece < 12; piece++)
	{
		Bitboard pieceBB = boardState.pieceBitboards[piece];
		if (pieceBB == 0)
			continue;

		evaluation += m_PieceValues[piece] * BitUtil::PopCnt(pieceBB) * pieceValueMultiplier;

		Bitboard squareBB = pieceBB;
		while (squareBB != 0)
		{
			uint8_t square = BitUtil::PopLSB(squareBB);

			uint8_t file = SquareUtil::GetFile(square);
			uint8_t rank = SquareUtil::GetRank(square);

			uint8_t whiteIndex = (7 - rank) * 8 + file;
			uint8_t blackIndex = square;


			switch (piece)
			{
			case PieceType::WHITE_PAWN:
				evaluation += m_PawnPositionValues[whiteIndex];
				break;
			case PieceType::WHITE_KNIGHT:
				evaluation += m_KnightPositionValues[whiteIndex];
				break;
			case PieceType::WHITE_BISHOP:
				evaluation += m_BishopPositionValues[whiteIndex];
				break;
			case PieceType::WHITE_ROOK:
				evaluation += m_RookPositionValues[whiteIndex];
				break;
			case PieceType::WHITE_QUEEN:
				evaluation += m_QueenPositionValues[whiteIndex];
				break;
			case PieceType::WHITE_KING:
				evaluation += m_KingPositionMiddleGameValues[whiteIndex] * (1 - endGame) + m_KingPositionEndGameValues[whiteIndex] * endGame;
				break;
			case PieceType::BLACK_PAWN:
				evaluation -= m_PawnPositionValues[blackIndex];
				break;
			case PieceType::BLACK_KNIGHT:
				evaluation -= m_KnightPositionValues[blackIndex];
				break;
			case PieceType::BLACK_BISHOP:
				evaluation -= m_BishopPositionValues[blackIndex];
				break;
			case PieceType::BLACK_ROOK:
				evaluation -= m_RookPositionValues[blackIndex];
				break;
			case PieceType::BLACK_QUEEN:
				evaluation -= m_QueenPositionValues[blackIndex];
				break;
			case PieceType::BLACK_KING:
				evaluation -= m_KingPositionMiddleGameValues[blackIndex] * (1 - endGame) + m_KingPositionEndGameValues[blackIndex] * endGame;
				break;
			default:
				break;
			}

		}
	}

	return evaluation;
}

void MyBotOld::SortMoves(const ChessBoard& board, MoveList& moves)
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
