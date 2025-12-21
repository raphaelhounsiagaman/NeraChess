#include "MyBotOld.h"

#include <algorithm>
#include <chrono>

ChessCore::Move MyBotOld::GetNextMove(const ChessCore::ChessBoard& givenBoard, const ChessCore::Clock& timer)
{
	ChessCore::ChessBoard board = givenBoard;

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
	if (legalMoves.size() < 2)
		return legalMoves[0];

	SortMoves(board, legalMoves);

	ChessCore::BoardState boardState = board.GetBoardState();
	bool whiteToPlay = boardState.HasFlag(ChessCore::BoardStateFlags::WhiteToMove);
	int8_t colorMultiplier = whiteToPlay ? 1 : -1;

	ChessCore::Move bestMove{};
	double bestEval = whiteToPlay ? -999999 : 999999;

	for (const ChessCore::Move& move : legalMoves)
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

double MyBotOld::Minimax(ChessCore::ChessBoard& board, int depth, bool whiteMaximizingPlayer, double alpha, double beta)
{
	uint16_t gameOverFlags = board.GetGameOver();

	double originAlpha = alpha;
	double originBeta = beta;

	if (gameOverFlags & ChessCore::IS_GAME_OVER)
	{
		if (gameOverFlags & ChessCore::IS_CHECKMATE)
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

	ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();


	double bestEval = whiteMaximizingPlayer ? -99999 : 99999;

	for (const ChessCore::Move& move : legalMoves)
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

double MyBotOld::EvaluateBoard(const ChessCore::BoardState& boardState, bool whiteToMove) const
{
	double evaluation = 0.0;

	float pieceValueMultiplier = 5.f;

	ChessCore::Bitboard importantPieces =
		boardState.pieceBitboards[ChessCore::WHITE_PAWN]   |
		boardState.pieceBitboards[ChessCore::WHITE_KNIGHT] |
		boardState.pieceBitboards[ChessCore::WHITE_BISHOP] |
		boardState.pieceBitboards[ChessCore::WHITE_ROOK]   |
		boardState.pieceBitboards[ChessCore::WHITE_QUEEN]  |
		boardState.pieceBitboards[ChessCore::WHITE_KING]   |
		boardState.pieceBitboards[ChessCore::BLACK_PAWN]   |
		boardState.pieceBitboards[ChessCore::BLACK_KNIGHT] |
		boardState.pieceBitboards[ChessCore::BLACK_BISHOP] |
		boardState.pieceBitboards[ChessCore::BLACK_ROOK]   |
		boardState.pieceBitboards[ChessCore::BLACK_QUEEN]  |
		boardState.pieceBitboards[ChessCore::BLACK_KING];

	float endGame = 1.0f - (float)(ChessCore::BitUtil::PopCnt(importantPieces) / 17);

	for (ChessCore::Piece piece = 0; piece < 12; piece++)
	{
		ChessCore::Bitboard pieceBB = boardState.pieceBitboards[piece];
		if (pieceBB == 0)
			continue;

		evaluation += m_PieceValues[piece] * ChessCore::BitUtil::PopCnt(pieceBB) * pieceValueMultiplier;

		ChessCore::Bitboard squareBB = pieceBB;
		while (squareBB != 0)
		{
			uint8_t square = ChessCore::BitUtil::PopLSB(squareBB);

			uint8_t file = ChessCore::SquareUtil::GetFile(square);
			uint8_t rank = ChessCore::SquareUtil::GetRank(square);

			uint8_t whiteIndex = (7 - rank) * 8 + file;
			uint8_t blackIndex = square;


			switch (piece)
			{
			case ChessCore::PieceType::WHITE_PAWN:
				evaluation += m_PawnPositionValues[whiteIndex];
				break;
			case ChessCore::PieceType::WHITE_KNIGHT:
				evaluation += m_KnightPositionValues[whiteIndex];
				break;
			case ChessCore::PieceType::WHITE_BISHOP:
				evaluation += m_BishopPositionValues[whiteIndex];
				break;
			case ChessCore::PieceType::WHITE_ROOK:
				evaluation += m_RookPositionValues[whiteIndex];
				break;
			case ChessCore::PieceType::WHITE_QUEEN:
				evaluation += m_QueenPositionValues[whiteIndex];
				break;
			case ChessCore::PieceType::WHITE_KING:
				evaluation += m_KingPositionMiddleGameValues[whiteIndex] * (1 - endGame) + m_KingPositionEndGameValues[whiteIndex] * endGame;
				break;
			case ChessCore::PieceType::BLACK_PAWN:
				evaluation -= m_PawnPositionValues[blackIndex];
				break;
			case ChessCore::PieceType::BLACK_KNIGHT:
				evaluation -= m_KnightPositionValues[blackIndex];
				break;
			case ChessCore::PieceType::BLACK_BISHOP:
				evaluation -= m_BishopPositionValues[blackIndex];
				break;
			case ChessCore::PieceType::BLACK_ROOK:
				evaluation -= m_RookPositionValues[blackIndex];
				break;
			case ChessCore::PieceType::BLACK_QUEEN:
				evaluation -= m_QueenPositionValues[blackIndex];
				break;
			case ChessCore::PieceType::BLACK_KING:
				evaluation -= m_KingPositionMiddleGameValues[blackIndex] * (1 - endGame) + m_KingPositionEndGameValues[blackIndex] * endGame;
				break;
			default:
				break;
			}

		}
	}

	return evaluation;
}

void MyBotOld::SortMoves(const ChessCore::ChessBoard& board, ChessCore::MoveList<218>& moves)
{
	std::array<float, 218> moveValues{};

	for (uint8_t i = 0; i < moves.size(); i++)
	{
		float moveScoreGuess = 0;
		ChessCore::Piece movePiece = ChessCore::MoveUtil::GetMovePiece(moves[i]);
		ChessCore::Piece capturePiece = board.GetPiece(ChessCore::MoveUtil::GetTargetSquare(moves[i]));

		if (capturePiece != ChessCore::PieceType::NO_PIECE)
			moveScoreGuess += 10 * m_PieceValues[capturePiece] - m_PieceValues[movePiece];
		if (ChessCore::MoveUtil::GetMoveFlags(moves[i]) & ChessCore::MoveFlags::IS_PROMOTION)
			moveScoreGuess += m_PieceValues[ChessCore::MoveUtil::GetPromoPiece(moves[i])];
		//if (board.SquareIsAttackedByOpponent(moves[i].TargetSquare))
		//	moveScoreGuess -= (float)(piece_values[move_piece_type]);

		if (ChessCore::MoveUtil::GetMoveFlags(moves[i]) & ChessCore::MoveFlags::IS_PROMOTION)
			moveScoreGuess += 10;

		moveValues[i] = moveScoreGuess;
	}
	
	struct MoveValuePair
	{
		ChessCore::Move move;
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
