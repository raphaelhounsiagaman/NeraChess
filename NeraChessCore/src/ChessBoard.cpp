#include "ChessBoard.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ChessBoardFlags.h"

#include "MoveGenerator.h"

#include "Util.h"

#include "Piece.h"
#include "Move.h"

ChessBoard::ChessBoard(const std::string& fen)
{
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
		m_BoardState.boardStateFlags |=
			character == 'K' ?
			(uint8_t)BoardStateFlags::CanWhiteCastleKing :
			character == 'Q' ?
			(uint8_t)BoardStateFlags::CanWhiteCastleQueen :
			character == 'k' ?
			(uint8_t)BoardStateFlags::CanBlackCastleKing :
			character == 'q' ?			 
			(uint8_t)BoardStateFlags::CanBlackCastleQueen :
			0;
	}

	if (fenParts[3][0] != '-')
	{
		uint8_t file = fenParts[3][1] - 'a';
		uint8_t rank = fenParts[3][0] - '1';

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
	return m_MoveGenerator.GenerateMoves(m_BoardState);
}

void ChessBoard::MakeMove(Move move)
{
	if (!(move.moveFlags & (uint8_t)MoveFlags::IS_VALID))
	{
		m_Error = 1;
		return;
	}

	m_HalfMoveClock++;

	bool whitesMove = move.movePieceType.IsWhite();
	if (!whitesMove)
		m_FullMoves++;


	// remove the piece from the start square
	m_BoardState.pieceBitboards[move.movePieceType.pieceType] &= ~(1ULL << move.startSquare);

	if (move.moveFlags & (uint8_t)MoveFlags::IS_CASTLES)
	{

		if (whitesMove)
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleKing;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanWhiteCastleQueen;
		}
		else
		{
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleKing;
			m_BoardState.boardStateFlags &= ~(uint8_t)BoardStateFlags::CanBlackCastleQueen;
		}


		bool queenSide = ChessUtil::SquareToFile(move.targetSquare) == 2;


		m_BoardState.pieceBitboards[move.movePieceType.pieceType] |= (1ULL << move.targetSquare);

		if (whitesMove)
		{
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] &= ~(1ULL << (queenSide ? 0 : 7)); // Remove the rook from the old square
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |= (1ULL << (queenSide ? 3 : 5)); // Move the rook to the correct square
		}
		else
		{
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] &= ~(1ULL << (queenSide ? 56 : 63)); // Remove the rook from the old square
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |= (1ULL << (queenSide ? 59 : 61)); // Move the rook to the correct square
		}
		
	}
	if (move.moveFlags & (uint8_t)MoveFlags::PAWN_TWO_UP)
	{
		m_BoardState.pieceBitboards[move.movePieceType.pieceType] |= (1ULL << move.targetSquare);
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
		m_BoardState.pieceBitboards[move.movePieceType.pieceType] |= (1ULL << move.targetSquare);
		m_BoardState.pieceBitboards[move.capturePieceType.pieceType] &= ~(1ULL << move.targetSquare);
		if (move.moveFlags & (uint8_t)MoveFlags::IS_EN_PASSANT)
		{
			uint8_t capturedPawnSquare = move.targetSquare + (move.movePieceType.pieceType == (uint8_t)PieceType::WHITE_PAWN ? 8 : -8);
			m_BoardState.pieceBitboards[move.capturePieceType.pieceType] &= ~(1ULL << capturedPawnSquare);
		}
	}
	if (move.moveFlags & (uint8_t)MoveFlags::IS_PROMOTION)
	{
		m_BoardState.pieceBitboards[move.movePieceType.pieceType] &= ~(1ULL << move.targetSquare);
		m_BoardState.pieceBitboards[move.promotionPieceType.pieceType] |= (1ULL << move.targetSquare);
	}
	
	
	m_BoardState.boardStateFlags ^= (uint8_t)BoardStateFlags::WhiteToMove; // Toggle the turn


	// TODO: Check if game is over



}
