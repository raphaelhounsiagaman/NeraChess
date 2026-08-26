#include "ChessBoard.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
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

namespace NeraChessEngine
{ 

	ChessBoard::ChessBoard(const std::string& fen)
	{
		m_MovesPlayed.reserve(100);

		std::istringstream fenStream(fen);
		std::array<std::string, 6> fenParts;
		for (std::string& part : fenParts)
		{
			if (!(fenStream >> part))
			{
				m_Error = 1;
				return;
			}
		}
		std::string extraPart;
		if (fenStream >> extraPart)
		{
			m_Error = 1;
			return;
		}

		int file = 0;
		int rank = 7;
		bool previousWasDigit = false;
		for (char character : fenParts[0])
		{
			if (character == '/')
			{
				if (file != 8 || rank == 0)
				{
					m_Error = 1;
					return;
				}
				--rank;
				file = 0;
				previousWasDigit = false;
				continue;
			}

			if (character >= '1' && character <= '8')
			{
				if (previousWasDigit || file + character - '0' > 8)
				{
					m_Error = 1;
					return;
				}
				file += character - '0';
				previousWasDigit = true;
				continue;
			}

			if (file >= 8)
			{
				m_Error = 1;
				return;
			}

			Piece piece = PieceType::NO_PIECE;
			switch (character)
			{
			case 'p': piece = PieceType::BLACK_PAWN; break;
			case 'n': piece = PieceType::BLACK_KNIGHT; break;
			case 'b': piece = PieceType::BLACK_BISHOP; break;
			case 'r': piece = PieceType::BLACK_ROOK; break;
			case 'q': piece = PieceType::BLACK_QUEEN; break;
			case 'k': piece = PieceType::BLACK_KING; break;
			case 'P': piece = PieceType::WHITE_PAWN; break;
			case 'N': piece = PieceType::WHITE_KNIGHT; break;
			case 'B': piece = PieceType::WHITE_BISHOP; break;
			case 'R': piece = PieceType::WHITE_ROOK; break;
			case 'Q': piece = PieceType::WHITE_QUEEN; break;
			case 'K': piece = PieceType::WHITE_KING; break;
			default:
				m_Error = 1;
				return;
			}
			m_BoardState.pieceBitboards[piece] |= 1ULL << (rank * 8 + file);
			++file;
			previousWasDigit = false;
		}
		if (rank != 0 || file != 8 ||
			BitUtil::PopCnt(m_BoardState.pieceBitboards[PieceType::WHITE_KING]) != 1 ||
			BitUtil::PopCnt(m_BoardState.pieceBitboards[PieceType::BLACK_KING]) != 1 ||
			((m_BoardState.pieceBitboards[PieceType::WHITE_PAWN] |
				m_BoardState.pieceBitboards[PieceType::BLACK_PAWN]) &
				(Square::Rank1 | Square::Rank8)))
		{
			m_Error = 1;
			return;
		}

		if (fenParts[1] == "w")
			m_BoardState.boardStateFlags |= BoardStateFlags::WhiteToMove;
		else if (fenParts[1] != "b")
		{
			m_Error = 1;
			return;
		}

		if (fenParts[2] != "-")
		{
			for (char character : fenParts[2])
			{
				uint8_t flag = 0;
				switch (character)
				{
				case 'K': flag = BoardStateFlags::CanWhiteCastleKing; break;
				case 'Q': flag = BoardStateFlags::CanWhiteCastleQueen; break;
				case 'k': flag = BoardStateFlags::CanBlackCastleKing; break;
				case 'q': flag = BoardStateFlags::CanBlackCastleQueen; break;
				default:
					m_Error = 1;
					return;
				}
				if (m_BoardState.boardStateFlags & flag)
				{
					m_Error = 1;
					return;
				}
				m_BoardState.boardStateFlags |= flag;
			}
		}

		if (fenParts[3] != "-")
		{
			const char expectedRank = m_BoardState.HasFlag(BoardStateFlags::WhiteToMove)
				? '6' : '3';
			if (fenParts[3].size() != 2 || fenParts[3][0] < 'a' ||
				fenParts[3][0] > 'h' || fenParts[3][1] != expectedRank)
			{
				m_Error = 1;
				return;
			}
			m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = fenParts[3][0] - 'a';
		}

		auto parseClock = [](const std::string& value, uint16_t& result, bool allowZero)
		{
			uint32_t parsed = 0;
			const auto [end, error] = std::from_chars(
				value.data(), value.data() + value.size(), parsed);
			if (error != std::errc{} || end != value.data() + value.size() ||
				parsed > std::numeric_limits<uint16_t>::max() || (!allowZero && parsed == 0))
			{
				return false;
			}
			result = static_cast<uint16_t>(parsed);
			return true;
		};
		if (!parseClock(fenParts[4], m_HalfMoveClock, true) ||
			!parseClock(fenParts[5], m_FullMoves, false))
		{
			m_Error = 1;
			return;
		}

		BoardState nonMovingSide = m_BoardState;
		nonMovingSide.boardStateFlags ^= BoardStateFlags::WhiteToMove;
		MoveGenerator legalityValidator;
		(void)legalityValidator.GenerateMoves(nonMovingSide);
		if (legalityValidator.InCheck())
		{
			m_Error = 1;
			return;
		}

		m_ZobristKey = Zobrist::CalculateZobristKey(*this);
		m_ZobristKeySet = true;
		m_RepetitionTable.AddEntry(GetRepetitionKey());

	}

	bool ChessBoard::operator==(const ChessBoard& other) const
	{
		bool same = true;

		if (m_RepetitionTable != other.m_RepetitionTable)
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


		m_ZobristKey = GetZobristKey();
		m_ZobristKeySet = true;

		m_WasBoardStateChanged = true;

		const Square startSquare = move.GetStartSquare();
		const Square targetSquare = move.GetTargetSquare();
		const Piece movePiece = move.GetMovePiece();
		const Piece promoPiece = move.GetPromoPiece();
		const uint8_t moveFlags = move.GetMoveFlags();

		const Bitboard startSquareBitboard = s_SquareBitboard[startSquare];
		const Bitboard targetSquareBitboard = s_SquareBitboard[targetSquare];

		const bool whitesMove = movePiece.IsWhite();

		const Piece capturedPiece = (moveFlags & MoveFlags::IS_EN_PASSANT)
			? (whitesMove ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN)
			: ((moveFlags & MoveFlags::IS_CAPTURE) ? GetPiece(targetSquare) : PieceType::NO_PIECE);

		UndoInfo info{};
		info.capturedPiece = capturedPiece;
		info.castlingRights = m_BoardState.GetCastlingRights();
		info.enPassantFile = m_BoardState.HasFlag(BoardStateFlags::CanEnPassant) ? m_BoardState.enPassantFile : 8;
		info.halfmoveClock = m_HalfMoveClock;
		info.zobristKey = m_ZobristKey;
		const uint8_t previousHashEnPassantFile = GetZobristEnPassantFile();

		if (!gameMove)
			m_UndoStack.push(info);

		Bitboard& movePieceBoard = m_BoardState.pieceBitboards[movePiece];
		movePieceBoard &= ~startSquareBitboard;
		movePieceBoard |= targetSquareBitboard;
		m_ZobristKey ^= Zobrist::piecesArray[movePiece][startSquare];
		m_ZobristKey ^= Zobrist::piecesArray[movePiece][targetSquare];
		m_ZobristKey ^= Zobrist::castlingRights[info.castlingRights];
		m_ZobristKey ^= Zobrist::enPassantFile[previousHashEnPassantFile];
		m_ZobristKey ^= Zobrist::sideToMove;

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
			if (!(moveFlags & MoveFlags::IS_EN_PASSANT))
				m_ZobristKey ^= Zobrist::piecesArray[capturedPiece][targetSquare];

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
				m_ZobristKey ^= Zobrist::piecesArray[capturedPiece][capturedPawnSquare];
			}
		}

		if (moveFlags & MoveFlags::IS_CASTLES)
		{

			bool queenSide = targetSquare.GetFile() == 2;

			if (whitesMove)
			{
				m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleKing;
				m_BoardState.boardStateFlags &= ~BoardStateFlags::CanWhiteCastleQueen;

				if (queenSide)
				{
					m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[0];
					m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[3];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::WHITE_ROOK][0];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::WHITE_ROOK][3];
				}
				else
				{
					m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] &= ~s_SquareBitboard[7];
					m_BoardState.pieceBitboards[PieceType::WHITE_ROOK] |= s_SquareBitboard[5];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::WHITE_ROOK][7];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::WHITE_ROOK][5];
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
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::BLACK_ROOK][56];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::BLACK_ROOK][59];
				}
				else
				{
					m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] &= ~s_SquareBitboard[63];
					m_BoardState.pieceBitboards[PieceType::BLACK_ROOK] |= s_SquareBitboard[61];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::BLACK_ROOK][63];
					m_ZobristKey ^= Zobrist::piecesArray[PieceType::BLACK_ROOK][61];
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
			m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = targetSquare % 8;
		}
		else
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = 8;
		}
	
		if (moveFlags & MoveFlags::IS_PROMOTION)
		{
			movePieceBoard &= ~targetSquareBitboard;
			m_BoardState.pieceBitboards[promoPiece] |= targetSquareBitboard;
			m_ZobristKey ^= Zobrist::piecesArray[movePiece][targetSquare];
			m_ZobristKey ^= Zobrist::piecesArray[promoPiece][targetSquare];
		}
	
		m_MovesPlayed.push_back(move);
	

		if (!whitesMove)
			m_FullMoves++;

		m_BoardState.boardStateFlags ^= BoardStateFlags::WhiteToMove;
		m_ZobristKey ^= Zobrist::castlingRights[m_BoardState.GetCastlingRights()];
		m_ZobristKey ^= Zobrist::enPassantFile[GetZobristEnPassantFile()];
		m_RepetitionTable.AddEntry(GetRepetitionKey());
	}


	void ChessBoard::UndoMove(Move move)
	{
		if (m_MovesPlayed.back() != move)
		{
			m_Error = 1;
			return;
		}

		m_WasBoardStateChanged = true;

		m_MovesPlayed.pop_back();
		UndoInfo info = m_UndoStack.pop();

		m_RepetitionTable.RemoveEntry(GetRepetitionKey());
		m_ZobristKey = info.zobristKey;
		m_ZobristKeySet = true;

		const bool whitesMove = move.GetMovePiece().IsWhite();

		const Square startSquare = move.GetStartSquare();
		const Square targetSquare = move.GetTargetSquare();
		const Piece movePiece = move.GetMovePiece();
		const Piece promoPiece = move.GetPromoPiece();
		const uint8_t moveFlags = move.GetMoveFlags();

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
			bool queenSide = targetSquare.GetFile() == 2;

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
			m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = info.enPassantFile;
		}
		else
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = 8;
		}
		if (moveFlags & MoveFlags::IS_PROMOTION)
		{
			m_BoardState.pieceBitboards[promoPiece] &= ~targetSquareBitboard;
		}
	
	}

	bool ChessBoard::MakeNullMove()
	{
		if (IsInCheck())
			return false;

		UndoInfo info{};
		info.castlingRights = m_BoardState.GetCastlingRights();
		info.enPassantFile = m_BoardState.HasFlag(BoardStateFlags::CanEnPassant)
			? m_BoardState.enPassantFile : 8;
		info.halfmoveClock = m_HalfMoveClock;
		info.zobristKey = GetZobristKey();
		const uint8_t previousHashEnPassantFile = GetZobristEnPassantFile();
		m_UndoStack.push(info);

		m_ZobristKey ^= Zobrist::enPassantFile[previousHashEnPassantFile];
		m_ZobristKey ^= Zobrist::enPassantFile[8];
		m_ZobristKey ^= Zobrist::sideToMove;
		m_BoardState.boardStateFlags ^= BoardStateFlags::WhiteToMove;
		m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassant;
		m_BoardState.enPassantFile = 8;
		m_WasBoardStateChanged = true;
		m_GameOverFlags = 0;

		return true;
	}

	void ChessBoard::UndoNullMove()
	{
		UndoInfo info = m_UndoStack.pop();
		m_BoardState.boardStateFlags ^= BoardStateFlags::WhiteToMove;
		if (info.enPassantFile < 8)
		{
			m_BoardState.boardStateFlags |= BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = info.enPassantFile;
		}
		else
		{
			m_BoardState.boardStateFlags &= ~BoardStateFlags::CanEnPassant;
			m_BoardState.enPassantFile = 8;
		}
		m_HalfMoveClock = info.halfmoveClock;
		m_ZobristKey = info.zobristKey;
		m_ZobristKeySet = true;
		m_WasBoardStateChanged = true;
		m_GameOverFlags = 0;
	}

	MoveList<218> ChessBoard::GetLegalMoves() const
	{
		return GetLegalMovesRef();
	}

	const MoveList<218>& ChessBoard::GetLegalMovesRef() const
	{
		if (m_WasBoardStateChanged)
		{
			m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
			m_WasBoardStateChanged = false;
		}
		return m_LegalMoves;
	}

	uint16_t ChessBoard::GetGameOver(bool gameCheck) const
	{
		uint16_t flags = GameOverFlags::IS_GAME_OVER;
		if (m_WasBoardStateChanged)
		{
			m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
			m_WasBoardStateChanged = false;
		}	
	
		if (m_LegalMoves.size() == 0)
		{

			if (m_MoveGenerator.InCheck())
			{
				flags |= GameOverFlags::IS_CHECKMATE;
				if (!m_BoardState.HasFlag(BoardStateFlags::WhiteToMove))
					flags |= GameOverFlags::IS_WHITE_WIN;
				m_GameOverFlags = flags;
				return flags;
			}

			flags |= GameOverFlags::IS_STALEMATE | GameOverFlags::IS_DRAW;
			m_GameOverFlags = flags;
			return flags;
		}

		if (m_HalfMoveClock >= 100)
		{
			flags |= GameOverFlags::IS_50MOVE_RULE | GameOverFlags::IS_DRAW;
			m_GameOverFlags = flags;
			return flags;
		}

		if (gameCheck &&
			m_RepetitionTable.GetRepetitionCount(GetRepetitionKey(), m_HalfMoveClock) >= 3)
		{
			flags |= GameOverFlags::IS_REPETITION | GameOverFlags::IS_DRAW;
			m_GameOverFlags = flags;
			return flags;
		}

		if (InsufficentMaterial(*this))
		{
			flags |= GameOverFlags::IS_INSUFFICIENT_MATERIAL | GameOverFlags::IS_DRAW;
			m_GameOverFlags = flags;
			return flags;
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

	uint64_t ChessBoard::GetRepetitionKey() const
	{
		return GetZobristKey();
	}

	uint8_t ChessBoard::GetZobristEnPassantFile() const
	{
		if (!m_BoardState.HasFlag(BoardStateFlags::CanEnPassant) || m_BoardState.enPassantFile >= 8)
			return 8;

		const bool whiteToMove = m_BoardState.HasFlag(BoardStateFlags::WhiteToMove);
		const uint8_t rank = whiteToMove ? 4 : 3;
		const uint8_t file = m_BoardState.enPassantFile;
		const Bitboard pawns = m_BoardState.pieceBitboards[
			whiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN];
		if (file > 0 && (pawns & s_SquareBitboard[rank * 8 + file - 1]))
			return file;
		if (file < 7 && (pawns & s_SquareBitboard[rank * 8 + file + 1]))
			return file;
		return 8;
	}

	std::string ChessBoard::GetFENString() const
	{
		std::string fen;
		fen.reserve(size_t(64 + 16));

		for (int rank = 7; rank >= 0; rank--)
		{

			uint8_t emptyCount = 0;

			for (uint8_t file = 0; file < 8; file++)
			{
				const Square square = rank * 8 + file;
				Piece piece = GetPiece(square);

				if (piece == PieceType::NO_PIECE)
				{
					emptyCount++;
					continue;
				}

				if (emptyCount > 0)
				{
					fen.push_back('0' + emptyCount);
					emptyCount = 0;
				}

				char c;
				switch (piece)
				{
				case PieceType::WHITE_PAWN:   c = 'P'; break;
				case PieceType::WHITE_KNIGHT: c = 'N'; break;
				case PieceType::WHITE_BISHOP: c = 'B'; break;
				case PieceType::WHITE_ROOK:   c = 'R'; break;
				case PieceType::WHITE_QUEEN:  c = 'Q'; break;
				case PieceType::WHITE_KING:   c = 'K'; break;

				case PieceType::BLACK_PAWN:   c = 'p'; break;
				case PieceType::BLACK_KNIGHT: c = 'n'; break;
				case PieceType::BLACK_BISHOP: c = 'b'; break;
				case PieceType::BLACK_ROOK:   c = 'r'; break;
				case PieceType::BLACK_QUEEN:  c = 'q'; break;
				case PieceType::BLACK_KING:   c = 'k'; break;

				default: c = '?'; break;
				}

				fen.push_back(c);
			}

			if (emptyCount > 0)
			{
				fen.push_back('0' + emptyCount);
				emptyCount = 0;
			}

			if (rank > 0)
				fen.push_back('/');
		}

		fen.push_back(' ');

		// 2. Side to move
		bool whiteToMove = (m_BoardState.boardStateFlags & BoardStateFlags::WhiteToMove);
		fen.push_back(whiteToMove ? 'w' : 'b');

		fen.push_back(' ');

		// 3. Castling rights
		bool anyCastle = false;
		if (m_BoardState.boardStateFlags & BoardStateFlags::CanWhiteCastleKing) { fen.push_back('K'); anyCastle = true; }
		if (m_BoardState.boardStateFlags & BoardStateFlags::CanWhiteCastleQueen) { fen.push_back('Q'); anyCastle = true; }
		if (m_BoardState.boardStateFlags & BoardStateFlags::CanBlackCastleKing) { fen.push_back('k'); anyCastle = true; }
		if (m_BoardState.boardStateFlags & BoardStateFlags::CanBlackCastleQueen) { fen.push_back('q'); anyCastle = true; }
		if (!anyCastle) fen.push_back('-');

		fen.push_back(' ');

		// 4. En passant
		bool enPassentAvailable = false;

		uint8_t checkRank = whiteToMove ? 4 : 3;

		Square leftSquare = checkRank * 8 + m_BoardState.enPassantFile - 1;
		Square rightSquare = checkRank * 8 + m_BoardState.enPassantFile + 1;

		if (leftSquare.GetRank() == checkRank)
		{
			Piece leftPiece = GetPiece(leftSquare);
			if (leftPiece == (whiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN))
			{
				enPassentAvailable = true;
			}
		}
		if (rightSquare.GetRank() == checkRank)
		{
			Piece rightPiece = GetPiece(rightSquare);
			if (rightPiece == (whiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN))
			{
				enPassentAvailable = true;
			}
		}


		if (!(m_BoardState.boardStateFlags & BoardStateFlags::CanEnPassant))
			enPassentAvailable = false;

		if (enPassentAvailable)
		{
			char file = 'a' + m_BoardState.enPassantFile;
			char rank = (whiteToMove ? '6' : '3');
			fen.push_back(file);
			fen.push_back(rank);
		}
		else
		{
			fen.push_back('-');
		}

		fen.push_back(' ');

		// 5. Halfmove clock
		fen.append(std::to_string(m_HalfMoveClock));

		fen.push_back(' ');

		// 6. Fullmove number
		fen.append(std::to_string(m_FullMoves));

		return fen;
	}

	uint64_t ChessBoard::PerfTest(int depth, ChessBoard& board)
	{
		MoveList<218> moveList = board.GetLegalMoves(); 
		uint64_t nodes = 0;

		if (depth == 1)
			return moveList.size();

		for (uint32_t i = 0; i < moveList.size(); i++) {
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

	bool ChessBoard::InsufficentMaterial(const ChessBoard& board)
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

		if (numWhiteKnights == 0 && numBlackKnights == 0)
		{
			Bitboard bishops = board.m_BoardState.pieceBitboards[PieceType::WHITE_BISHOP] |
				board.m_BoardState.pieceBitboards[PieceType::BLACK_BISHOP];
			bool sawLightSquare = false;
			bool sawDarkSquare = false;
			while (bishops)
			{
				const Square square = BitUtil::PopLSB(bishops);
				sawLightSquare |= square.IsLightSquare();
				sawDarkSquare |= !square.IsLightSquare();
			}
			return !(sawLightSquare && sawDarkSquare);
		}

		return false;
	}

	bool ChessBoard::IsInCheck() const
	{
		const bool whiteToMove = m_BoardState.HasFlag(BoardStateFlags::WhiteToMove);
		const uint8_t friendlyOffset = whiteToMove ? 0 : 6;
		const uint8_t enemyOffset = whiteToMove ? 6 : 0;

		const Bitboard king = m_BoardState.pieceBitboards[friendlyOffset + PieceType::WHITE_KING];
		if (!king)
			return false;
		const uint8_t kingSquare = BitUtil::GetLSBIndex(king);

		Bitboard occupancy = 0;
		for (const Bitboard pieceSet : m_BoardState.pieceBitboards)
			occupancy |= pieceSet;

		const Bitboard enemyPawns = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_PAWN];
		const Bitboard pawnCheckers = whiteToMove
			? MoveGenerator::s_WhitePawnAttackMasks[kingSquare]
			: MoveGenerator::s_BlackPawnAttackMasks[kingSquare];
		if (pawnCheckers & enemyPawns)
			return true;

		const Bitboard enemyKnights = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_KNIGHT];
		if (MoveGenerator::s_KnightMoveMask[kingSquare] & enemyKnights)
			return true;

		const Bitboard enemyBishops = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_BISHOP];
		const Bitboard enemyRooks = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_ROOK];
		const Bitboard enemyQueens = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_QUEEN];
		if (MoveGenerator::LookupBishopAttacks(kingSquare, occupancy) & (enemyBishops | enemyQueens))
			return true;
		if (MoveGenerator::LookupRookAttacks(kingSquare, occupancy) & (enemyRooks | enemyQueens))
			return true;

		const Bitboard enemyKing = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_KING];
		if (MoveGenerator::s_KingMoveMask[kingSquare] & enemyKing)
			return true;

		return false;
	}

	bool ChessBoard::IsInCheckByMoveGeneration() const
	{
		if (m_WasBoardStateChanged)
		{
			m_LegalMoves = m_MoveGenerator.GenerateMoves(m_BoardState);
			m_WasBoardStateChanged = false;
		}
		return m_MoveGenerator.InCheck();
	}

	bool ChessBoard::IsRuleDraw() const
	{
		if (m_HalfMoveClock >= 100)
			return true;
		if (m_RepetitionTable.GetRepetitionCount(GetRepetitionKey(), m_HalfMoveClock) >= 3)
			return true;
		return InsufficentMaterial(*this);
	}

	bool ChessBoard::GivesCheck(Move move) const
	{
		const Piece movePiece = move.GetMovePiece();
		const bool whitesMove = movePiece.IsWhite();
		const uint8_t offset = whitesMove ? 0 : 6;
		const uint8_t enemyOffset = whitesMove ? 6 : 0;

		const Bitboard enemyKing = m_BoardState.pieceBitboards[enemyOffset + PieceType::WHITE_KING];
		if (!enemyKing)
			return false;
		const uint8_t enemyKingSquare = BitUtil::GetLSBIndex(enemyKing);

		const uint8_t startSquare = move.GetStartSquare();
		const uint8_t targetSquare = move.GetTargetSquare();
		const uint8_t moveFlags = move.GetMoveFlags();

		Bitboard occupancy = 0;
		for (const Bitboard pieceSet : m_BoardState.pieceBitboards)
			occupancy |= pieceSet;

		// Replay the move on local copies of only the boards that can change: the
		// occupancy, the friendly attackers, and the castling rook.
		Bitboard pawns = m_BoardState.pieceBitboards[offset + PieceType::WHITE_PAWN];
		Bitboard knights = m_BoardState.pieceBitboards[offset + PieceType::WHITE_KNIGHT];
		Bitboard bishops = m_BoardState.pieceBitboards[offset + PieceType::WHITE_BISHOP];
		Bitboard rooks = m_BoardState.pieceBitboards[offset + PieceType::WHITE_ROOK];
		Bitboard queens = m_BoardState.pieceBitboards[offset + PieceType::WHITE_QUEEN];

		const Bitboard startBitboard = s_SquareBitboard[startSquare];
		const Bitboard targetBitboard = s_SquareBitboard[targetSquare];
		occupancy &= ~startBitboard;
		occupancy |= targetBitboard;

		if (moveFlags & MoveFlags::IS_EN_PASSANT)
		{
			const uint8_t capturedSquare = whitesMove
				? static_cast<uint8_t>(targetSquare - 8)
				: static_cast<uint8_t>(targetSquare + 8);
			occupancy &= ~s_SquareBitboard[capturedSquare];
		}
		else if (moveFlags & MoveFlags::IS_CASTLES)
		{
			const bool queenSide = Square(targetSquare).GetFile() == 2;
			const uint8_t rookFrom = whitesMove ? (queenSide ? 0 : 7) : (queenSide ? 56 : 63);
			const uint8_t rookTo = whitesMove ? (queenSide ? 3 : 5) : (queenSide ? 59 : 61);
			occupancy &= ~s_SquareBitboard[rookFrom];
			occupancy |= s_SquareBitboard[rookTo];
			rooks &= ~s_SquareBitboard[rookFrom];
			rooks |= s_SquareBitboard[rookTo];
		}

		const auto relocate = [&](Bitboard& board) { board &= ~startBitboard; };
		relocate(pawns);
		relocate(knights);
		relocate(bishops);
		relocate(rooks);
		relocate(queens);

		const uint8_t landedType = (moveFlags & MoveFlags::IS_PROMOTION)
			? static_cast<uint8_t>(move.GetPromoPiece() % 6)
			: static_cast<uint8_t>(movePiece % 6);
		switch (landedType)
		{
		case PieceType::WHITE_PAWN:
			pawns |= targetBitboard;
			break;
		case PieceType::WHITE_KNIGHT:
			knights |= targetBitboard;
			break;
		case PieceType::WHITE_BISHOP:
			bishops |= targetBitboard;
			break;
		case PieceType::WHITE_ROOK:
			rooks |= targetBitboard;
			break;
		case PieceType::WHITE_QUEEN:
			queens |= targetBitboard;
			break;
		default:
			// A king can never give check itself, but it still blocks and unblocks rays,
			// which the shared occupancy above already accounts for.
			break;
		}

		// Testing attacks outward from the enemy king covers direct checks and
		// discovered checks in the same pass.
		const Bitboard pawnCheckers = whitesMove
			? MoveGenerator::s_BlackPawnAttackMasks[enemyKingSquare]
			: MoveGenerator::s_WhitePawnAttackMasks[enemyKingSquare];
		if (pawnCheckers & pawns)
			return true;
		if (MoveGenerator::s_KnightMoveMask[enemyKingSquare] & knights)
			return true;
		if (MoveGenerator::LookupBishopAttacks(enemyKingSquare, occupancy) & (bishops | queens))
			return true;
		if (MoveGenerator::LookupRookAttacks(enemyKingSquare, occupancy) & (rooks | queens))
			return true;
		return false;
	}


} // namespace NeraChessEngine
