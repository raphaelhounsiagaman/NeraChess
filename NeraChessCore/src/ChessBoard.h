#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

#include "ChessBoardFlags.h"

#include "Piece.h"
#include "Move.h"

typedef uint64_t Bitboard;

class ChessBoard
{
public:
	// Starting FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1

	ChessBoard(const std::string& fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	~ChessBoard() = default;

    const std::vector<Move>& GetLegalMoves() const { return m_LegalMoves; }
    Piece GetPiece(const uint8_t square) const { return m_Pieces[square]; }
    uint8_t GetError() const { return m_Error; }

private:

    uint8_t m_Error = 0;

    uint16_t m_ChessBoardFlags = 0;

    std::vector<Move> m_LegalMoves = {};
    std::array<Piece, 64> m_Pieces = {};

    uint8_t m_HalfMoveClock = 0;
    uint16_t m_FullMoves = 0;

    uint8_t m_EnPassentSquare = 65;

    Bitboard m_AllPieces = 0;

    Bitboard m_WhitePieces = 0;

    Bitboard m_WhitePawns = 0;
    Bitboard m_WhiteKnights = 0;
    Bitboard m_WhiteBishops = 0;
    Bitboard m_WhiteRooks = 0;
    Bitboard m_WhiteQueens = 0;
    Bitboard m_WhiteKing = 0;

    Bitboard m_BlackPieces = 0;

    Bitboard m_BlackPawns = 0;
    Bitboard m_BlackKnights = 0;
    Bitboard m_BlackBishops = 0;
    Bitboard m_BlackRooks = 0;
    Bitboard m_BlackQueens = 0;
    Bitboard m_BlackKing = 0;

};