#include "ChessBoard.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

#include "MoveGenerator.h"
	
#include "ChessUtil.h"

#include "Zobrist.h"
#include "Piece.h"
#include "Move.h"

#ifdef  DEBUG
	#if defined(_MSC_VER)
		#define DEBUG_BREAK() __debugbreak()
	#elif defined(__APPLE__) && defined(__MACH__)
		#define DEBUG_BREAK() __builtin_debugtrap()
	#elif defined(__GNUC__)
		#define DEBUG_BREAK() __builtin_trap()
	#else
		#include <signal.h>
		#define DEBUG_BREAK() raise(SIGTRAP)
	#endif
#endif //  DEBUG

ChessBoard::ChessBoard(const std::string& fen)
{
	m_MovesPlayed.reserve(100);

	std::istringstream fenStream(fen);
	std::string part;
	std::vector<std::string> fenParts;

	while (fenStream >> part)
	{
		fenParts.push_back(part);
	}

	if (fenParts.size() != 6)
	{
		std::printf("Invalid FEN string: %s\n", fen.c_str());
		m_Error = 1;
		return;
	}

	uint8_t file = 0;
	uint8_t rank = 7;
	for (char character : fenParts[0])
	{
		switch (character)
		{
		case 'p':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'n':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'b':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'r':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'q':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'k':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'P':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'N':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'B':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'R':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'Q':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'K':
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING] |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case '-': case '1':
			file++;
			break;
		case '2':
			file += 2;
			break;
		case '3':
			file += 3;
			break;
		case '4':
			file += 4;
			break;
		case '5':
			file += 5;
			break;
		case '6':
			file += 6;
			break;
		case '7':
			file += 7;
			break;
		case '8':
			file += 8;
			break;
		case '/':
			file = 0;
			rank--;
			break;
		default:
			break;
		}
	}

	m_BoardState.boardStateFlags |= fenParts[1][0] == 'w' ? (uint8_t)BoardStateFlags::WhiteToMove : 0;

	for (char character : fenParts[2])
	{
		switch (character)
		{
		case 'K':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
			break;
		case 'Q':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
			break;
		case 'k':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
			break;
		case 'q':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
			break;
		default:
			break;
		}

	}

	if (fenParts[3][0] != '-')
	{
		uint8_t file = fenParts[3][0] - 'a';
		uint8_t rank = fenParts[3][1] - '1';

		if (file > 7 || rank > 7)
		{
			std::printf("Invalid en passant square in FEN string: %s\n", fenParts[3].c_str());
			m_Error = 1;
			return;
		}
		else
		{
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanEnPassent;
			m_BoardState.enPassantFile = file;
		}
	}

	m_HalfMoveClock = std::stoi(fenParts[4]);

	m_FullMoves = std::stoi(fenParts[5]);

	m_RepetitionTable.AddEntry(m_BoardState.pieceBitboards);

	m_ZobristKey = Zobrist::CalculateZobristKey(*this);

}

bool ChessBoard::operator==(const ChessBoard& other) const
{
	bool same = true;

	if (m_RepetitionTable != m_RepetitionTable)
		same = false;
	else if (m_BoardState != other.m_BoardState)
		same = false;
	else if (m_MovesPlayed != other.m_MovesPlayed)
		same = false;
	else if (m_GameOverFlags != other.m_GameOverFlags)
		same = false;
	else if (m_HalfMoveClock != other.m_HalfMoveClock)
		same = false;
	else if (m_FullMoves != other.m_FullMoves)
		same = false;

	return same;
}

Piece ChessBoard::GetPiece(const uint8_t square) const
{

	return
		(m_BoardState.pieceBitboards[PieceType::WHITE_PAWN] >> square) & 1ULL ? PieceType::WHITE_PAWN  :
		(m_BoardState.pieceBitboards[PieceType::WHITE_KNIGHT] >> square) & 1ULL ? PieceType::WHITE_KNIGHT :
		(m_BoardState.pieceBitboards[PieceType::WHITE_BISHOP] >> square) & 1ULL ? PieceType::WHITE_BISHOP :
		(m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] >> square) & 1ULL ? PieceType::WHITE_ROOK :
		(m_BoardState.pieceBitboards[PieceType::WHITE_QUEEN] >> square) & 1ULL ? PieceType::WHITE_QUEEN :
		(m_BoardState.pieceBitboards[PieceType::WHITE_KING] >> square) & 1ULL ? PieceType::WHITE_KING :

		(m_BoardState.pieceBitboards[PieceType::BLACK_PAWN] >> square) & 1ULL ? PieceType::BLACK_PAWN :
		(m_BoardState.pieceBitboards[PieceType::BLACK_KNIGHT] >> square) & 1ULL ? PieceType::BLACK_KNIGHT :
		(m_BoardState.pieceBitboards[PieceType::BLACK_BISHOP] >> square) & 1ULL ? PieceType::BLACK_BISHOP :
		(m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] >> square) & 1ULL ? PieceType::BLACK_ROOK :
		(m_BoardState.pieceBitboards[PieceType::BLACK_QUEEN] >> square) & 1ULL ? PieceType::BLACK_QUEEN :
		(m_BoardState.pieceBitboards[PieceType::BLACK_KING] >> square) & 1ULL ? PieceType::BLACK_KING :
		PieceType::NO_PIECE;
}

void ChessBoard::MakeMove(Move move, bool gameMove)
{
	if (!move)
	{
		m_Error = 1;
		return;
	}


	m_ZobristKeySet = false;

	m_WasBoardStateChanged = true;

	const Square startSquare = MoveUtil::GetFromSquare(move);
	const Square targetSquare = MoveUtil::GetTargetSquare(move);
	const Piece movePiece = MoveUtil::GetMovePiece(move);
	const Piece promoPiece = MoveUtil::GetPromoPiece(move);
	const uint8_t moveFlags = MoveUtil::GetMoveFlags(move);

	const Bitboard startSquareBitboard = s_SquareBitboard[startSquare];
	const Bitboard targetSquareBitboard = s_SquareBitboard[targetSquare];

	const bool whitesMove = PieceUtil::IsWhite(movePiece);

	const Piece capturedPiece =
		(moveFlags & MoveFlags::IS_EN_PASSANT) 
		? (whitesMove ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN)
		: GetPiece(MoveUtil::GetTargetSquare(move));

	UndoInfo info{};
	info.capturedPiece = capturedPiece;
	info.castlingRights = m_BoardState.GetCastlingRights();
	info.enPassantFile = m_BoardState.HasFlag(BoardStateFlags::CanEnPassent) ? m_BoardState.enPassantFile : 8;
	info.halfmoveClock = m_HalfMoveClock;

	if (!gameMove)
		m_UndoStack.push(info);

	// remove the piece from the start square
	m_BoardState.pieceBitboards[movePiece] &= ~startSquareBitboard;
	m_BoardState.pieceBitboards[movePiece] |= targetSquareBitboard;

	Bitboard& movePieceBoard = m_BoardState.pieceBitboards[movePiece];
	movePieceBoard &= ~startSquareBitboard;
	movePieceBoard |= targetSquareBitboard;

	m_HalfMoveClock++;
	if (movePiece == PieceType::WHITE_PAWN || movePiece == PieceType::BLACK_PAWN || (moveFlags & MoveFlags::IS_CAPTURE))
	{
		m_HalfMoveClock = 0;
		if (gameMove)
			m_RepetitionTable.Clear();
	}

	if (moveFlags & MoveFlags::IS_CAPTURE)
	{
		m_BoardState.pieceBitboards[capturedPiece] &= ~targetSquareBitboard;

		if (capturedPiece == PieceType::WHITE_ROOK && targetSquare == 0)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleQueen;
		}
		else if (capturedPiece == PieceType::WHITE_ROOK && targetSquare == 7)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleKing;
		}
		else if (capturedPiece == PieceType::BLACK_ROOK && targetSquare == 56)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleQueen;
		}
		else if (capturedPiece == PieceType::BLACK_ROOK && targetSquare == 63)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleKing;
		}
		else if (moveFlags & MoveFlags::IS_EN_PASSANT)
		{
			uint8_t capturedPawnSquare = targetSquare + (movePiece == PieceType::WHITE_PAWN ? -8 : 8);
			m_BoardState.pieceBitboards[capturedPiece] &= ~(s_SquareBitboard[capturedPawnSquare]);
		}
	}

	if (moveFlags & MoveFlags::IS_CASTLES)
	{

		bool queenSide = SquareUtil::GetFile(targetSquare) == 2;

		if (whitesMove)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleKing;
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleQueen;

			if (queenSide)
			{
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[0];
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[3];
			}
			else
			{
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[7];
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[5];
			}

		}
		else
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleKing;
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleQueen;

			if (queenSide)
			{
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] &= ~s_SquareBitboard[56];
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] |= s_SquareBitboard[59];
			}
			else
			{
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] &= ~s_SquareBitboard[63];
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] |= s_SquareBitboard[61];
			}


		}

	}
	else if (movePiece == PieceType::WHITE_KING)
	{
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleQueen;
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleKing;
	}
	else if (movePiece == PieceType::BLACK_KING)
	{
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleQueen;
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleKing;
	}
	else if (movePiece == PieceType::WHITE_ROOK)
	{
		if (startSquare == 0)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleQueen;
		}
		else if (startSquare == 7)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleKing;
		}
	}
	else if (movePiece == PieceType::BLACK_ROOK)
	{
		if (startSquare == 56)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleQueen;
		}
		else if (startSquare == 63)
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanBlackCastleKing;
		}
	}

	if (moveFlags & MoveFlags::PAWN_TWO_UP)
	{
		m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassent;
		m_BoardState.enPassantFile = targetSquare % 8;
	}
	else
	{
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassent;
		m_BoardState.enPassantFile = 8;
	}
	
	if (moveFlags & MoveFlags::IS_PROMOTION)
	{
		movePieceBoard &= ~targetSquareBitboard;
		m_BoardState.pieceBitboards[promoPiece] |= targetSquareBitboard;
	}
	
	m_MovesPlayed.push_back(move);
	
	if (gameMove)
		m_RepetitionTable.AddEntry(m_BoardState.pieceBitboards);

	if (!whitesMove)
		m_FullMoves++;

	m_BoardState.boardStateFlags ^= BoardStateFlags::WhiteToMove;
}


void ChessBoard::UndoMove(Move move)
{
	if (m_MovesPlayed.back() != move)
	{
		m_Error = 1;
		return;
	}

	m_ZobristKeySet = false;

	m_WasBoardStateChanged = true;

	m_MovesPlayed.pop_back();
	UndoInfo info = m_UndoStack.pop();

	const bool whitesMove = PieceUtil::IsWhite(MoveUtil::GetMovePiece(move));

	const Square startSquare = MoveUtil::GetFromSquare(move);
	const Square targetSquare = MoveUtil::GetTargetSquare(move);
	const Piece movePiece = MoveUtil::GetMovePiece(move);
	const Piece promoPiece = MoveUtil::GetPromoPiece(move);
	const uint8_t moveFlags = MoveUtil::GetMoveFlags(move);

	const Bitboard startSquareBitboard = s_SquareBitboard[startSquare];
	const Bitboard targetSquareBitboard = s_SquareBitboard[targetSquare];

	Bitboard& movePieceBoard = m_BoardState.pieceBitboards[movePiece];

	m_BoardState.boardStateFlags ^= BoardStateFlags::WhiteToMove;

	m_BoardState.boardStateFlags |= info.castlingRights;
	m_GameOverFlags = 0;

	if (!whitesMove)
		m_FullMoves--;

	m_HalfMoveClock = info.halfmoveClock;

	movePieceBoard |= startSquareBitboard;
	movePieceBoard &= ~targetSquareBitboard;

	if (moveFlags & MoveFlags::IS_CAPTURE)
	{
		if (!(moveFlags & MoveFlags::IS_EN_PASSANT))
		{
			m_BoardState.pieceBitboards[info.capturedPiece] |= targetSquareBitboard;
		}
		else if (moveFlags & MoveFlags::IS_EN_PASSANT)
		{
			uint8_t capturedPawnSquare = targetSquare + (movePiece == PieceType::WHITE_PAWN ? -8 : 8);
			m_BoardState.pieceBitboards[info.capturedPiece] |= s_SquareBitboard[capturedPawnSquare];
		}
	}

	if (moveFlags & MoveFlags::IS_CASTLES)
	{
		bool queenSide = SquareUtil::GetFile(targetSquare) == 2;

		if (whitesMove)
		{
			if (queenSide)
			{
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[0];
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[3];
			}
			else
			{
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[7];
				m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[5];
			}

		}
		else
		{
			if (queenSide)
			{
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] |= s_SquareBitboard[56];
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] &= ~s_SquareBitboard[59];
			}
			else
			{
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] |= s_SquareBitboard[63];
				m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] &= ~s_SquareBitboard[61];
			}

		}
	}

	if (info.enPassantFile != 8)
	{
		m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassent;
		m_BoardState.enPassantFile = info.enPassantFile;
	}
	else
	{
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassent;
		m_BoardState.enPassantFile = 8;
	}
	if (moveFlags & MoveFlags::IS_PROMOTION)
	{
		m_BoardState.pieceBitboards[promoPiece] &= ~targetSquareBitboard;
	}
	
}

MoveList ChessBoard::GetLegalMoves() const
{
	if (m_WasBoardStateChanged)
	{
		m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
		m_WasBoardStateChanged = false;
	}
	return m_LegalMoves;
}

uint16_t ChessBoard::GetGameOver(bool gameCheck)
{
	if (m_WasBoardStateChanged)
	{
		m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
		m_WasBoardStateChanged = false;
	}	
	
	m_GameOverFlags |= GameOverFlags::IS_GAME_OVER;

	if (m_LegalMoves.size() == 0)
	{

		if (m_MoveGenerator.InCheck())
		{
			m_GameOverFlags |= GameOverFlags::IS_CHECKMATE;
			return m_GameOverFlags;
		}
		
		m_GameOverFlags |= GameOverFlags::IS_STALEMATE;
		return m_GameOverFlags;
	}

	if (m_HalfMoveClock >= 50)
	{
		m_GameOverFlags |= GameOverFlags::IS_50MOVE_RULE;
		return m_GameOverFlags;
	}

	if (gameCheck && m_RepetitionTable.GetRepetitionCount(m_BoardState.pieceBitboards) >= 3)
	{
		m_GameOverFlags |= GameOverFlags::IS_REPETITION;
		return m_GameOverFlags;
	}

	if (InsufficentMaterial(*this))
	{
		m_GameOverFlags |= GameOverFlags::IS_INSUFFICIENT_MATERIAL;
		return m_GameOverFlags;
	}

	m_GameOverFlags = 0;
	return m_GameOverFlags;
}

uint64_t ChessBoard::GetZobristKey() const
{
	if (m_ZobristKeySet)
		return m_ZobristKey;

	m_ZobristKey = Zobrist::CalculateZobristKey(*this);
	m_ZobristKeySet = true;
	return m_ZobristKey;
}

void ChessBoard::RunPerformanceTest(ChessBoard& board, int calcDepth)
{
	if (calcDepth <= 0)
	{
		std::cout << "Invalid depth for performance test. Must be greater than 0.\n";
		return;
	}

	auto start = std::chrono::steady_clock::now();

	MoveList move_list = board.GetLegalMoves();
	uint64_t result = 0;

	if (calcDepth == 1)
	{
		for (int i = 0; i < move_list.size(); i++)
		{
			std::cout <<
				SquareUtil::SquareAsString(MoveUtil::GetFromSquare(move_list[i])) <<
				SquareUtil::SquareAsString(MoveUtil::GetTargetSquare(move_list[i])) <<
				": " << "1" << "\n";
		}
		result += move_list.size();

		auto end = std::chrono::steady_clock::now();
		auto duration = duration_cast<std::chrono::milliseconds>(end - start);

		std::cout << "\nNodes searched: " << result << "\n";
		std::cout << "Time Elapsed: " << duration.count() << " ms (" << (result * 1000) / duration.count() << " nodes/s)\n\n";

		return;
	}

	for (int i = 0; i < move_list.size(); i++)
	{
#ifdef DEBUG
		ChessBoard temp_board = board;
#endif // DEBUG
		board.MakeMove(move_list[i]);
#ifdef DEBUG
		ChessBoard midBoard = board;
#endif // DEBUG
		uint64_t perftResult = PerfTest(calcDepth - 1, board);
		std::cout << 
			SquareUtil::SquareAsString(MoveUtil::GetFromSquare(move_list[i])) <<
			SquareUtil::SquareAsString(MoveUtil::GetTargetSquare(move_list[i])) <<
			": " << perftResult << "\n";
		result += perftResult;
		board.UndoMove(move_list[i]);
#ifdef DEBUG
		if (temp_board != board)
		{
			std::cout << "Error: Board state changed after undoing move.\n";
			DEBUG_BREAK();
		}
#endif // DEBUG
	}

	auto end = std::chrono::steady_clock::now();
	auto duration = duration_cast<std::chrono::milliseconds>(end - start);


	std::cout << "\nNodes searched: " << result << "\n";
	std::cout << "Time Elapsed: " << duration.count() << " ms (" << (result * 1000) / duration.count() << " nodes/s)\n\n";
}



uint64_t ChessBoard::PerfTest(int depth, ChessBoard& board)
{
	MoveList moveList = board.GetLegalMoves(); 
	uint64_t nodes = 0;

	if (depth == 1)
		return moveList.size();

	for (int i = 0; i < moveList.size(); i++) {
#ifdef DEBUG
		ChessBoard temp_board = board;
#endif // DEBUG
		board.MakeMove(moveList[i]);
#ifdef DEBUG
		ChessBoard midBoard = board;
#endif // DEBUG
		nodes += PerfTest(depth - 1, board);
		board.UndoMove(moveList[i]);
#ifdef DEBUG
		if (temp_board != board || board.m_Error != 0)
		{
			std::cout << "Error: Board state changed after undoing move.\n";
			DEBUG_BREAK();
		}
#endif // DEBUG
	}
	return nodes;
}

bool ChessBoard::InsufficentMaterial(ChessBoard board)
{
	if (board.m_BoardState.pieceBitboards[PieceType::WHITE_PAWN]   | 
		board.m_BoardState.pieceBitboards[PieceType::WHITE_ROOK]   |
		board.m_BoardState.pieceBitboards[PieceType::WHITE_QUEEN]  |
		board.m_BoardState.pieceBitboards[PieceType::BLACK_PAWN]   | 
		board.m_BoardState.pieceBitboards[PieceType::BLACK_ROOK]   | 
		board.m_BoardState.pieceBitboards[PieceType::BLACK_QUEEN])
	{
		return false;
	}

	uint8_t numWhiteBishops = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[PieceType::WHITE_BISHOP]);
	uint8_t numBlackBishops = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[PieceType::BLACK_BISHOP]);
	uint8_t numWhiteKnights = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[PieceType::WHITE_KNIGHT]);
	uint8_t numBlackKnights = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[PieceType::BLACK_KNIGHT]);
	uint8_t numWhiteMinors = numWhiteBishops + numWhiteKnights;
	uint8_t numBlackMinors = numBlackBishops + numBlackKnights;
	uint8_t numMinors = numWhiteMinors + numBlackMinors;

	if (numMinors <= 1)
	{
		return true;
	}

	if (numMinors == 2 && numWhiteBishops == 1 && numBlackBishops == 1)
	{
		bool whiteBishopIsLightSquare = SquareUtil::LightSquare(BitUtil::GetLSBIndex(board.m_BoardState.pieceBitboards[PieceType::WHITE_BISHOP]));
		bool blackBishopIsLightSquare = SquareUtil::LightSquare(BitUtil::GetLSBIndex(board.m_BoardState.pieceBitboards[PieceType::BLACK_BISHOP]));
		return whiteBishopIsLightSquare == blackBishopIsLightSquare;
	}

	return false;
}

bool ChessBoard::IsInCheck()
{
	if (m_WasBoardStateChanged)
	{
		m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
		m_WasBoardStateChanged = false;
	}
	return m_MoveGenerator.InCheck();
}