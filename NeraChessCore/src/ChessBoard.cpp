#include "ChessBoard.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ChessBoardFlags.h"

#include "Piece.h"
#include "Move.h"

ChessBoard::ChessBoard(const std::string& fen)
{
	// Reserve space for legal moves (218 is max possible in any position)
	m_LegalMoves.reserve(218);

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
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::PAWN | PieceTypeFlags::BLACK_PIECE };
			m_BlackPawns |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'n':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::KNIGHT | PieceTypeFlags::BLACK_PIECE };
			m_BlackKnights |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'b':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::BISHOP | PieceTypeFlags::BLACK_PIECE };
			m_BlackBishops |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'r':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::ROOK | PieceTypeFlags::BLACK_PIECE };
			m_BlackRooks |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'q':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::QUEEN | PieceTypeFlags::BLACK_PIECE };
			m_BlackQueens |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'k':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::KING | PieceTypeFlags::BLACK_PIECE };
			m_BlackKing |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'P':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::PAWN | PieceTypeFlags::WHITE_PIECE };
			m_WhitePawns |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'N':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::KNIGHT | PieceTypeFlags::WHITE_PIECE };
			m_WhiteKnights |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'B':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::BISHOP | PieceTypeFlags::WHITE_PIECE };
			m_WhiteBishops |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'R':
			m_Pieces[rank * 8 + file] = { PieceTypeFlags::ROOK | PieceTypeFlags::WHITE_PIECE };
			m_WhiteRooks |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'Q':
			m_Pieces[file] = { PieceTypeFlags::QUEEN | PieceTypeFlags::WHITE_PIECE };
			m_WhiteQueens |= 1ULL << (rank * 8 + file);
			file++;
			break;
		case 'K':
			m_Pieces[file] = { PieceTypeFlags::KING | PieceTypeFlags::WHITE_PIECE };
			m_WhiteKing |= 1ULL << (rank * 8 + file);
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

	m_WhitePieces = m_WhitePawns | m_WhiteKnights | m_WhiteBishops | m_WhiteRooks | m_WhiteQueens | m_WhiteKing;
	m_BlackPieces = m_BlackPawns | m_BlackKnights | m_BlackBishops | m_BlackRooks | m_BlackQueens | m_BlackKing;
	m_AllPieces = m_WhitePieces | m_BlackPieces;

	m_ChessBoardFlags |= fenParts[1][0] == 'w' ? ChessBoardFlags::WHITE_TO_MOVE : 0;

	for (char character : fenParts[2])
	{
		m_ChessBoardFlags |=
			character == 'K' ?
			CAN_WHITE_CASTLE_KING :
			character == 'Q' ?
			CAN_WHITE_CASTLE_QUEEN :
			character == 'k' ?
			CAN_BLACK_CASTLE_KING :
			character == 'q' ?
			CAN_BLACK_CASTLE_QUEEN :
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
			m_EnPassentSquare = rank * 8 + file;
		}
	}

	m_HalfMoveClock = std::stoi(fenParts[4]);

	m_FullMoves = std::stoi(fenParts[5]);
}
