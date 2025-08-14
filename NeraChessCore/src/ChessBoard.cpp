#include "ChessBoard.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

#include "GameOverFlags.h"

#include "MoveGenerator.h"

#include "Util.h"

#include "Piece.h"
#include "Move.h"

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

	m_FirstWhiteKingMove = ~(1U);
	m_FirstBlackKingMove = ~(1U);

	m_FirstWhiteKingRookMove = ~(1U);
	m_FirstWhiteQueenRookMove = ~(1U);
	m_FirstBlackKingRookMove = ~(1U);
	m_FirstBlackQueenRookMove = ~(1U);

	for (char character : fenParts[2])
	{
		switch (character)
		{
		case 'K':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
			m_FirstWhiteKingMove = 0;
			m_FirstWhiteKingRookMove = 0;
			break;
		case 'Q':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
			m_FirstWhiteKingMove = 0;
			m_FirstWhiteQueenRookMove = 0;
			break;
		case 'k':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
			m_FirstBlackKingMove = 0;
			m_FirstBlackKingRookMove = 0;
			break;
		case 'q':
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
			m_FirstBlackKingMove = 0;
			m_FirstBlackQueenRookMove = 0;
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
			m_BoardState.enPassentFile = file;
		}
	}

	m_HalfMoveClock = std::stoi(fenParts[4]);

	m_FullMoves = std::stoi(fenParts[5]);
}

bool ChessBoard::operator==(const ChessBoard& other) const
{
	bool same = true;

	if (m_BoardState != other.m_BoardState)
		same = false;
	if (m_MovesPlayed != other.m_MovesPlayed)
		same = false;
	if (m_GameOverFlags != other.m_GameOverFlags)
		same = false;
	if (m_HalfMoveClock != other.m_HalfMoveClock)
		same = false;
	if (m_FullMoves != other.m_FullMoves)
		same = false;
	if (m_FirstBlackKingMove != other.m_FirstBlackKingMove)
		same = false;
	if (m_FirstWhiteKingMove != other.m_FirstWhiteKingMove)
		same = false;
	if (m_FirstWhiteKingRookMove != other.m_FirstWhiteKingRookMove)
		same = false;
	if (m_FirstWhiteQueenRookMove != other.m_FirstWhiteQueenRookMove)
		same = false;
	if (m_FirstBlackKingRookMove != other.m_FirstBlackKingRookMove)
		same = false;
	if (m_FirstBlackQueenRookMove != other.m_FirstBlackQueenRookMove)
		same = false;



	/*
	if (m_BoardState == other.m_BoardState &&
		m_MovesPlayed == other.m_MovesPlayed &&
		m_GameOverFlags == other.m_GameOverFlags &&
		m_HalfMoveClock == other.m_HalfMoveClock &&
		m_FullMoves == other.m_FullMoves &&
		m_FirstBlackKingMove == other.m_FirstBlackKingMove &&
		m_FirstWhiteKingMove == other.m_FirstWhiteKingMove &&
		m_FirstWhiteKingRookMove == other.m_FirstWhiteKingRookMove &&
		m_FirstWhiteQueenRookMove == other.m_FirstWhiteQueenRookMove &&
		m_FirstBlackKingRookMove == other.m_FirstBlackKingRookMove &&
		m_FirstBlackQueenRookMove == other.m_FirstBlackQueenRookMove
		)
		return true;
	*/

	return same;
}

Piece ChessBoard::GetPiece(const uint8_t square) const
{

	return
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_PAWN } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_KNIGHT } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_BISHOP } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_ROOK } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_QUEEN } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::WHITE_KING } :

		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_PAWN } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_KNIGHT } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_BISHOP } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_ROOK } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_QUEEN } :
		(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING] >> square) & 1ULL ? Piece{ (uint8_t)PieceType::BLACK_KING } :
		Piece{ (uint8_t)PieceType::NO_PIECE };
}

std::vector<Move> ChessBoard::GetLegalMoves() const
{
	if (m_GameOverFlags & (uint8_t)GameOverFlags::IS_GAME_OVER)
		return {};
	return m_MoveGenerator.GenerateMoves(m_BoardState);
}

void ChessBoard::MakeMove(Move move)
{
	if (!(move.moveFlags & (uint8_t)MoveFlags::IS_VALID))
	{
		m_Error = 1;
		return;
	}

	bool whitesMove = move.movePieceType.IsWhite();

	// remove the piece from the start square
	m_BoardState.pieceBitboards[move.movePieceType.pieceType] &= ~(1ULL << move.startSquare);
	m_BoardState.pieceBitboards[move.movePieceType.pieceType] |= (1ULL << move.targetSquare);

	if (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_KING && m_FirstWhiteKingMove == 0)
	{
		m_FirstWhiteKingMove = m_FullMoves;
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleKing;
	}
	else if (move.movePieceType.pieceType == (uint8_t)PieceType::BLACK_KING && m_FirstBlackKingMove == 0)
	{
		m_FirstBlackKingMove = m_FullMoves;
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleQueen;
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleKing;
	}

	if (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK)
	{

		if (move.startSquare == 0 && m_FirstWhiteQueenRookMove == 0)
		{
			m_FirstWhiteQueenRookMove = m_FullMoves;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		}
		else if (move.startSquare == 7 && m_FirstWhiteKingRookMove == 0)
		{
			m_FirstWhiteKingRookMove = m_FullMoves;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleKing;
		}

	}
	else if (move.movePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK)
	{

		if (move.startSquare == 56 && m_FirstBlackQueenRookMove == 0)
		{
			m_FirstBlackQueenRookMove = m_FullMoves;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleQueen;
		}
		else if (move.startSquare == 63 && m_FirstBlackKingRookMove == 0)
		{
			m_FirstBlackKingRookMove = m_FullMoves;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleKing;
		}
	}


	if (move.moveFlags & (uint8_t)MoveFlags::IS_CASTLES)
	{

		bool queenSide = ChessUtil::SquareToFile(move.targetSquare) == 2;

		if (whitesMove)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleKing;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleQueen;

			if (queenSide)
			{
				if (m_FirstWhiteQueenRookMove == 0)
					m_FirstWhiteQueenRookMove = m_FullMoves;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] &= ~(1ULL <<  0); // Remove the rook from the old square
				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= (1ULL << 3); // Move the rook to the correct square
			}
			else
			{
				if (m_FirstWhiteKingRookMove == 0)
					m_FirstWhiteKingRookMove = m_FullMoves;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] &= ~(1ULL << 7); // Remove the rook from the old square
				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= (1ULL << 5); // Move the rook to the correct square
			}
			
			
		}
		else
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleKing;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleQueen;

			if (queenSide)
			{
				if (m_FirstBlackQueenRookMove == 0)
					m_FirstBlackQueenRookMove = m_FullMoves;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] &= ~(1ULL << 56); // Remove the rook from the old square
				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= (1ULL << 59); // Move the rook to the correct square
			}
			else
			{
				if (m_FirstBlackKingRookMove == 0)
					m_FirstBlackKingRookMove = m_FullMoves;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] &= ~(1ULL << 63); // Remove the rook from the old square
				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= (1ULL << 61); // Move the rook to the correct square
			}
			

		}
		
	}
	if (move.moveFlags & (uint8_t)MoveFlags::PAWN_TWO_UP)
	{
		m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanEnPassent;
		m_BoardState.enPassentFile = move.targetSquare % 8; // Set the en passant file to the file of the target square
	}
	else
	{
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanEnPassent;
		m_BoardState.enPassentFile = 8; // Reset the en passant file
	}
	if (move.moveFlags & (uint8_t)MoveFlags::IS_CAPTURE)
	{
		m_BoardState.pieceBitboards[move.capturePieceType.pieceType] &= ~(1ULL << move.targetSquare);

		if (move.capturePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK && move.targetSquare == 0)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleQueen;
			if (m_FirstWhiteQueenRookMove == 0)
				m_FirstWhiteQueenRookMove = m_FullMoves;
		}
		if (move.capturePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK && move.targetSquare == 7)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleKing;
			if (m_FirstWhiteKingRookMove == 0)
				m_FirstWhiteKingRookMove = m_FullMoves;
		}
		if (move.capturePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK && move.targetSquare == 56)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleQueen;
			if (m_FirstBlackQueenRookMove == 0)
				m_FirstBlackQueenRookMove = m_FullMoves;
		}
		if (move.capturePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK && move.targetSquare == 63)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleKing;
			if (m_FirstBlackKingRookMove == 0)
				m_FirstBlackKingRookMove = m_FullMoves;
		}


		if (move.moveFlags & (uint8_t)MoveFlags::IS_EN_PASSANT)
		{
			uint8_t capturedPawnSquare = move.targetSquare + (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_PAWN ? -8 : 8);
			m_BoardState.pieceBitboards[move.capturePieceType.pieceType] &= ~(1ULL << capturedPawnSquare);
		}
	}
	if (move.moveFlags & (uint8_t)MoveFlags::IS_PROMOTION)
	{
		m_BoardState.pieceBitboards[move.movePieceType.pieceType] &= ~(1ULL << move.targetSquare);
		m_BoardState.pieceBitboards[move.promotionPieceType.pieceType] |= (1ULL << move.targetSquare);
	}
	
	
	m_BoardState.boardStateFlags ^= (uint8_t)BoardStateFlags::WhiteToMove; // Toggle the turn

	m_MovesPlayed.push_back(move); // Store the move in the history


	m_HalfMoveClock++;

	if (!whitesMove)
		m_FullMoves++;

	MoveGenerator generator{};
	std::vector<Move> moves = generator.GenerateMoves(m_BoardState);

	m_GameOverFlags = 0;

	// CheckMate and Stalemate
	if (moves.size() == 0)
	{
		if (generator.InCheck())
		{
			m_GameOverFlags |= (uint8_t)GameOverFlags::IS_GAME_OVER;
			m_GameOverFlags |= (uint8_t)GameOverFlags::IS_CHECKMATE;
			return;
		}
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_GAME_OVER;
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_STALEMATE;
		return;
	}

	// Fifty move rule
	if (m_HalfMoveClock >= 100)
	{
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_GAME_OVER;
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_50MOVE_RULE;
		return;
	}

	// Threefold repetition
	int repCount = 2;//board.RepetitionPositionHistory.Count((x = > x == board.currentGameState.zobristKey));
	if (repCount == 3)
	{
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_GAME_OVER;
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_REPETITION;
		return;
	}

	// Look for insufficient material
	if (InsufficentMaterial(*this))
	{
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_GAME_OVER;
		m_GameOverFlags |= (uint8_t)GameOverFlags::IS_INSUFFICIENT_MATERIAL;
		return;
	}

}


void ChessBoard::UndoMove(Move move)
{

	if (!(move.moveFlags & (uint8_t)MoveFlags::IS_VALID))
	{
		m_Error = 1;
		return;
	}
	if (m_MovesPlayed[m_MovesPlayed.size() - 1] != move)
	{
		m_Error = 1;
		return;
	}

	m_GameOverFlags = 0; // Reset game over flags

	bool whitesMove = move.movePieceType.IsWhite();

	if (!whitesMove)
		m_FullMoves--;

	m_HalfMoveClock--;

	m_MovesPlayed.pop_back(); // Remove the last move from the history

	m_BoardState.boardStateFlags ^= (uint8_t)BoardStateFlags::WhiteToMove; // Toggle the turn

	if (move.moveFlags & (uint8_t)MoveFlags::IS_PROMOTION)
	{
		m_BoardState.pieceBitboards[move.promotionPieceType.pieceType] &= ~(1ULL << move.targetSquare);
	}
	if (move.moveFlags & (uint8_t)MoveFlags::IS_CAPTURE)
	{
		if (!(move.moveFlags & (uint8_t)MoveFlags::IS_EN_PASSANT))
		{
			m_BoardState.pieceBitboards[move.capturePieceType.pieceType] |= (1ULL << move.targetSquare);
		}
			

		if (move.moveFlags & (uint8_t)MoveFlags::IS_EN_PASSANT)
		{
			uint8_t capturedPawnSquare = move.targetSquare + (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_PAWN ? -8 : 8);
			m_BoardState.pieceBitboards[move.capturePieceType.pieceType] |= 1ULL << capturedPawnSquare;
		}


		if (move.capturePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK && 
			move.targetSquare == 0 && 
			m_FirstWhiteQueenRookMove == m_FullMoves 			
			)
		{
			m_FirstWhiteQueenRookMove = 0;
			if (m_FirstWhiteKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		}

		if (move.capturePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK && 
			move.targetSquare == 7 && 
			m_FirstWhiteKingRookMove == m_FullMoves
			)
		{
			m_FirstWhiteKingRookMove = 0;
			if (m_FirstWhiteKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
		}

		if (move.capturePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK && 
			move.targetSquare == 56 && 
			m_FirstBlackQueenRookMove == m_FullMoves 
			)
		{
			m_FirstBlackQueenRookMove = 0;
			if (m_FirstBlackKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
		}

		if (move.capturePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK && 
			move.targetSquare == 63 && 
			m_FirstBlackKingRookMove == m_FullMoves 
			)
		{
			m_FirstBlackKingRookMove = 0;
			if (m_FirstBlackKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
		}

	}
	if (m_MovesPlayed.size() > 0 && m_MovesPlayed[m_MovesPlayed.size() - 1].moveFlags & (uint8_t)MoveFlags::PAWN_TWO_UP)
	{
		m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanEnPassent;
		m_BoardState.enPassentFile = m_MovesPlayed[m_MovesPlayed.size() - 1].targetSquare % 8; // Set the en passant file to the file of the target square
	}
	else
	{
		m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanEnPassent;
		m_BoardState.enPassentFile = 8; // Reset the en passant file
	}

	if (move.moveFlags & (uint8_t)MoveFlags::IS_CASTLES)
	{

		bool queenSide = ChessUtil::SquareToFile(move.targetSquare) == 2;

		if (whitesMove)
		{


			if (queenSide)
			{
				if (m_FirstWhiteKingRookMove == 0)
					m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;

				if (m_FirstWhiteQueenRookMove == m_FullMoves)
					m_FirstWhiteQueenRookMove = 0;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= (1ULL << 0);
				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] &= ~(1ULL << 3); 
			}
			else
			{
				if (m_FirstWhiteQueenRookMove == 0)
					m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;

				if (m_FirstWhiteKingRookMove == m_FullMoves)
					m_FirstWhiteKingRookMove = 0;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= (1ULL << 7);
				m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] &= ~(1ULL << 5); 
			}

		}
		else
		{

			if (queenSide)
			{
				if (m_FirstBlackKingRookMove == 0)
					m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;

				if (m_FirstBlackQueenRookMove == m_FullMoves)
					m_FirstBlackQueenRookMove = 0;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= (1ULL << 56);
				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] &= ~(1ULL << 59);
			}
			else
			{
				if (m_FirstBlackQueenRookMove == 0)
					m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
				
				if (m_FirstBlackKingRookMove == m_FullMoves)
					m_FirstBlackKingRookMove = 0;

				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= (1ULL << 63);
				m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] &= ~(1ULL << 61);
			}

		}

	}

	if (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_ROOK)
	{

		if (move.startSquare == 0 && m_FirstWhiteQueenRookMove == m_FullMoves)
		{
			m_FirstWhiteQueenRookMove = 0;
			if (m_FirstWhiteKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		}
		else if (move.startSquare == 7 && m_FirstWhiteKingRookMove == m_FullMoves)
		{
			m_FirstWhiteKingRookMove = 0;
			if (m_FirstWhiteKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
		}

	}
	else if (move.movePieceType.pieceType == (uint8_t)PieceType::BLACK_ROOK)
	{

		if (move.startSquare == 56 && m_FirstBlackQueenRookMove == m_FullMoves)
		{
			m_FirstBlackQueenRookMove = 0;
			if (m_FirstBlackKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
		}
		else if (move.startSquare == 63 && m_FirstBlackKingRookMove == m_FullMoves)
		{
			m_FirstBlackKingRookMove = 0;
			if (m_FirstBlackKingMove == 0)
				m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
		}
	}

	if (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_KING && m_FirstWhiteKingMove == m_FullMoves)
	{
		m_FirstWhiteKingMove = 0;
		if (m_FirstWhiteQueenRookMove == 0)
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		if (m_FirstWhiteKingRookMove == 0)
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanWhiteCastleKing;
	}
	else if (move.movePieceType.pieceType == (uint8_t)PieceType::BLACK_KING && m_FirstBlackKingMove == m_FullMoves)
	{
		m_FirstBlackKingMove = 0;
		if (m_FirstBlackQueenRookMove == 0)
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleQueen;
		if (m_FirstBlackKingRookMove == 0)								 
			m_BoardState.boardStateFlags |= (uint8_t)BoardStateFlags::CanBlackCastleKing;
	}

	// restore the piece to the start square
	m_BoardState.pieceBitboards[move.movePieceType.pieceType] |= (1ULL << move.startSquare);
	m_BoardState.pieceBitboards[move.movePieceType.pieceType] &= ~(1ULL << move.targetSquare);


	m_GameOverFlags = 0;

}


void ChessBoard::RunPerformanceTest(ChessBoard board, int calcDepth)
{
	const std::array<uint64_t, 14> perftExpectedResults = {
								1,
							   20,
							  400,
							8'902,
						  197'281,
						4'865'609,
					  119'060'324,
				   	3'195'901'860,
				   84'998'978'956,
				2'439'530'234'167,
			   69'352'859'712'417,
			2'097'651'003'696'806,
		   62'854'969'236'701'747,
		1'981'066'775'000'396'239,
	};

	if (calcDepth <= 0)
	{
		std::cout << "Invalid depth for performance test. Must be greater than 0.\n";
		return;
	}

	auto start = std::chrono::steady_clock::now();

	std::vector<Move> move_list = board.GetLegalMoves();
	size_t n_moves = move_list.size();
	uint64_t result = 0;

	for (int i = 0; i < n_moves; i++) {
		ChessBoard temp_board = board;
		board.MakeMove(move_list[i]);
		ChessBoard midBoard = board;
		uint64_t perftResult = PerfTest(calcDepth - 1, board);
		std::cout << 
			ChessUtil::SquareAsString(move_list[i].startSquare) <<
			ChessUtil::SquareAsString(move_list[i].targetSquare) <<
			": " << perftResult << "\n";
		result += perftResult;
		board.UndoMove(move_list[i]);

		if (temp_board != board)
		{
			std::cout << "Error: Board state changed after undoing move.\n";
			board.m_Error = 1;
		}
	}

	auto end = std::chrono::steady_clock::now();
	auto duration = duration_cast<std::chrono::milliseconds>(end - start);


	std::cout << "\nNodes searched: " << result << "\n";
	std::cout << "Time Elapsed: " << duration.count() << " ms (" << (result * 1000) / duration.count() << " nodes/s)\n";





}

uint64_t ChessBoard::PerfTest(int depth, ChessBoard board)
{
	std::vector<Move> move_list;
	move_list.reserve(218); // Reserve space for moves to avoid reallocations
	move_list = board.GetLegalMoves();
	int n_moves;
	uint64_t nodes = 0;

	n_moves = move_list.size();

	if (depth == 1)
		return (uint64_t)n_moves;

	for (int i = 0; i < n_moves; i++) {
		ChessBoard temp_board = board;
		board.MakeMove(move_list[i]);
		ChessBoard midBoard = board;
		nodes += PerfTest(depth - 1, board);
		board.UndoMove(move_list[i]);
		if (temp_board != board || board.m_Error != 0)
		{
			std::cout << "Error: Board state changed after undoing move.\n";
			board.m_Error = 1;
		}
	}
	return nodes;
	/*
	if (depth == 0)
		return 1ULL;

	std::vector<Move> move_list = board.GetLegalMoves();
	size_t n_moves = move_list.size();
	uint64_t nodes = 0;

	for (int i = 0; i < n_moves; i++) {
		ChessBoard temp_board = board;
		board.MakeMove(move_list[i]);
		nodes += PerfTest(depth - 1, board);
		ChessBoard midBoard = board;
		board.UndoMove(move_list[i]);
		if (temp_board != board || board.m_Error != 0)
		{
			std::cout << "Error: Board state changed after undoing move.\n";
			board.m_Error = 1;
		}

	}
	return nodes;
	*/
}

bool ChessBoard::InsufficentMaterial(ChessBoard board)
{
	

	if (board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN] > 0 || 
		board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] > 0 ||
		board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN] > 0 ||
		board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN] > 0 || 
		board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] > 0 || 
		board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN] > 0)
	{
		return false;
	}



	// If no pawns, queens, or rooks on the board, then consider knight and bishop cases
	int numWhiteBishops = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP]);
	int numBlackBishops = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP]);
	int numWhiteKnights = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT]);
	int numBlackKnights = BitUtil::PopCnt(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT]);
	int numWhiteMinors = numWhiteBishops + numWhiteKnights;
	int numBlackMinors = numBlackBishops + numBlackKnights;
	int numMinors = numWhiteMinors + numBlackMinors;

	// Lone kings or King vs King + single minor: is insuffient
	if (numMinors <= 1)
	{
		return true;
	}

	// Bishop vs bishop: is insufficient when bishops are same colour complex
	if (numMinors == 2 && numWhiteBishops == 1 && numBlackBishops == 1)
	{
		bool whiteBishopIsLightSquare = ChessUtil::LightSquare(BitUtil::PopLSB(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP]));
		bool blackBishopIsLightSquare = ChessUtil::LightSquare(BitUtil::PopLSB(board.m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP]));
		return whiteBishopIsLightSquare == blackBishopIsLightSquare;
	}

	return false;
}

