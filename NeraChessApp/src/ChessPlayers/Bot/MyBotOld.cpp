#include "MyBotOld.h"

#include <algorithm>
#include <chrono>

NeraChessEngine::Move MyBotOld::GetNextMove(const NeraChessEngine::ChessBoard& givenBoard, const NeraChessEngine::Clock& timer)
{
	NeraChessEngine::ChessBoard board = givenBoard;

	NeraChessEngine::MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() < 2)
		return legalMoves[0];

	SortMoves(board, legalMoves);

	NeraChessEngine::BoardState boardState = board.GetBoardState();
	bool whiteToPlay = boardState.HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove);
	NeraChessEngine::Move bestMove{};
	double bestEval = whiteToPlay ? -999999 : 999999;

	for (const NeraChessEngine::Move& move : legalMoves)
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

double MyBotOld::Minimax(NeraChessEngine::ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta)
{
	uint16_t gameOverFlags = board.GetGameOver();

	double originAlpha = alpha;
	double originBeta = beta;

	if (gameOverFlags & NeraChessEngine::IS_GAME_OVER)
	{
		if (gameOverFlags & NeraChessEngine::IS_CHECKMATE)
			return whiteMaximizingPlayer ? -99999 : 99999;
		else
			return 0;
	}
	else if (depth == 0)
	{
		return EvaluateBoard(board.GetBoardState(), whiteMaximizingPlayer);
	}

	uint64_t zobristKey = board.GetZobristKey();
	TTEntryMyBotOld entry;

	if (m_TranspositionTable.TryGetValue(zobristKey, entry))
	{
		// If the stored depth is greater or equal to the current depth, use the stored evaluation
		if (entry.depth >= depth)
		{
			if (entry.type == EntryFlagMyBotOld::Exact)
			{
				return entry.evaluation;
			}
			else if (entry.type == EntryFlagMyBotOld::LowerBound && entry.evaluation > alpha)
			{
				alpha = entry.evaluation;
			}
			else if (entry.type == EntryFlagMyBotOld::UpperBound && entry.evaluation < beta)
			{
				beta = entry.evaluation;
			}

			if (alpha >= beta)
			{
				return entry.evaluation;
			}
		}
	}

	NeraChessEngine::MoveList<218> legalMoves = board.GetLegalMoves();


	double bestEval = whiteMaximizingPlayer ? -99999 : 99999;

	for (const NeraChessEngine::Move& move : legalMoves)
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

	EntryFlagMyBotOld type;
	if (bestEval <= originAlpha)
	{
		type = EntryFlagMyBotOld::UpperBound;
	}
	else if (bestEval >= originBeta)
	{
		type = EntryFlagMyBotOld::LowerBound;
	}
	else
	{
		type = EntryFlagMyBotOld::Exact;
	}

	// Store the result in the transposition table
	m_TranspositionTable.StoreEntry(zobristKey, TTEntryMyBotOld
		(
			bestEval,
			depth,
			type
		));

	return bestEval;
}

double MyBotOld::EvaluateBoard(const NeraChessEngine::BoardState& boardState, bool whiteToMove) const
{
	double evaluation = 0.0;

	float pieceValueMultiplier = 5.f;

	NeraChessEngine::Bitboard importantPieces =
		boardState.pieceBitboards[NeraChessEngine::WHITE_PAWN]   |
		boardState.pieceBitboards[NeraChessEngine::WHITE_KNIGHT] |
		boardState.pieceBitboards[NeraChessEngine::WHITE_BISHOP] |
		boardState.pieceBitboards[NeraChessEngine::WHITE_ROOK]   |
		boardState.pieceBitboards[NeraChessEngine::WHITE_QUEEN]  |
		boardState.pieceBitboards[NeraChessEngine::WHITE_KING]   |
		boardState.pieceBitboards[NeraChessEngine::BLACK_PAWN]   |
		boardState.pieceBitboards[NeraChessEngine::BLACK_KNIGHT] |
		boardState.pieceBitboards[NeraChessEngine::BLACK_BISHOP] |
		boardState.pieceBitboards[NeraChessEngine::BLACK_ROOK]   |
		boardState.pieceBitboards[NeraChessEngine::BLACK_QUEEN]  |
		boardState.pieceBitboards[NeraChessEngine::BLACK_KING];

	float endGame = 1.0f - (float)(NeraChessEngine::BitUtil::PopCnt(importantPieces) / 17);

	for (NeraChessEngine::Piece piece = 0; piece < 12; piece++)
	{
		NeraChessEngine::Bitboard pieceBB = boardState.pieceBitboards[piece];
		if (pieceBB == 0)
			continue;

		evaluation += m_PieceValues[piece] * NeraChessEngine::BitUtil::PopCnt(pieceBB) * pieceValueMultiplier;

		NeraChessEngine::Bitboard squareBB = pieceBB;
		while (squareBB != 0)
		{
			NeraChessEngine::Square square(NeraChessEngine::BitUtil::PopLSB(squareBB));

			uint8_t file = square.GetFile();
			uint8_t rank = square.GetRank();

			uint8_t whiteIndex = (7 - rank) * 8 + file;
			uint8_t blackIndex = square;


			switch (piece)
			{
			case NeraChessEngine::PieceType::WHITE_PAWN:
				evaluation += m_PawnPositionValues[whiteIndex];
				break;
			case NeraChessEngine::PieceType::WHITE_KNIGHT:
				evaluation += m_KnightPositionValues[whiteIndex];
				break;
			case NeraChessEngine::PieceType::WHITE_BISHOP:
				evaluation += m_BishopPositionValues[whiteIndex];
				break;
			case NeraChessEngine::PieceType::WHITE_ROOK:
				evaluation += m_RookPositionValues[whiteIndex];
				break;
			case NeraChessEngine::PieceType::WHITE_QUEEN:
				evaluation += m_QueenPositionValues[whiteIndex];
				break;
			case NeraChessEngine::PieceType::WHITE_KING:
				evaluation += m_KingPositionMiddleGameValues[whiteIndex] * (1 - endGame) + m_KingPositionEndGameValues[whiteIndex] * endGame;
				break;
			case NeraChessEngine::PieceType::BLACK_PAWN:
				evaluation -= m_PawnPositionValues[blackIndex];
				break;
			case NeraChessEngine::PieceType::BLACK_KNIGHT:
				evaluation -= m_KnightPositionValues[blackIndex];
				break;
			case NeraChessEngine::PieceType::BLACK_BISHOP:
				evaluation -= m_BishopPositionValues[blackIndex];
				break;
			case NeraChessEngine::PieceType::BLACK_ROOK:
				evaluation -= m_RookPositionValues[blackIndex];
				break;
			case NeraChessEngine::PieceType::BLACK_QUEEN:
				evaluation -= m_QueenPositionValues[blackIndex];
				break;
			case NeraChessEngine::PieceType::BLACK_KING:
				evaluation -= m_KingPositionMiddleGameValues[blackIndex] * (1 - endGame) + m_KingPositionEndGameValues[blackIndex] * endGame;
				break;
			default:
				break;
			}

		}
	}

	return evaluation;
}

void MyBotOld::SortMoves(const NeraChessEngine::ChessBoard& board, NeraChessEngine::MoveList<218>& moves)
{
	std::array<float, 218> moveValues{};

	for (uint8_t i = 0; i < moves.size(); i++)
	{
		float moveScoreGuess = 0;
		NeraChessEngine::Piece movePiece = moves[i].GetMovePiece();
		NeraChessEngine::Piece capturePiece = board.GetPiece(moves[i].GetTargetSquare());

		if (capturePiece != NeraChessEngine::PieceType::NO_PIECE)
			moveScoreGuess += 10 * m_PieceValues[capturePiece] - m_PieceValues[movePiece];
		if (moves[i].GetMoveFlags() & NeraChessEngine::MoveFlags::IS_PROMOTION)
			moveScoreGuess += m_PieceValues[moves[i].GetPromoPiece()];
		//if (board.SquareIsAttackedByOpponent(moves[i].TargetSquare))
		//	moveScoreGuess -= (float)(piece_values[move_piece_type]);

		if (moves[i].GetMoveFlags() & NeraChessEngine::MoveFlags::IS_PROMOTION)
			moveScoreGuess += 10;

		moveValues[i] = moveScoreGuess;
	}
	
	struct MoveValuePair
	{
		NeraChessEngine::Move move;
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
