#pragma once

#include <vector>
#include <memory>

#include "Move.h"
#include "MoveList.h"
#include "BoardState.h"

class MoveGenerator
{	
public:
	MoveGenerator() = default;
	~MoveGenerator() = default;

	MoveList GenerateMoves(const BoardState& board);

	bool InCheck() const { return m_InCheck; };

private:

	void InitGen();

	void CalculateAttackMaps();
	void GenSlidingAttacks();

	void CalculateKingMoves();
	void CalculateSlidingMoves();
	void CalculateKnightMoves();
	void CalculatePawnMoves();

	void GeneratePromotions(uint8_t startSquare, uint8_t targetSquare);

	Bitboard GetSlidingAttacks(uint8_t square, Bitboard blockers, bool orthogonal);

	PieceType GetPiece(uint8_t square);
	
	bool IsPinned(int square) const;

	bool InCheckAfterEnPassant(int startSquare, int targetSquare, int epCaptureSquare);

	Bitboard PawnAttacks(Bitboard pawns, bool isWhite);

private:

	MoveList m_LegalMoves;
	
	BoardState m_BoardState{}; // check

	bool m_WhiteToMove{}; // check

	Bitboard m_WhitePieces{};  // check
	Bitboard m_BlackPieces{}; // check
	Bitboard m_AllPieces{};// check

	Bitboard m_FriendlyPieces{};// check
	Bitboard m_OpponentPieces{};// check

	uint8_t m_FriendlyKingSquare{};// check
	uint8_t m_OpponentKingSquare{};// check

	Bitboard m_FriendlyOrthogonalSliders{};// check
	Bitboard m_FriendlyDiagonalSliders{};  // check

	Bitboard m_OpponentOrthogonalSliders{};// check
	Bitboard m_OpponentDiagonalSliders{};  // check

	Bitboard m_OpponentAttackMap{};

	Bitboard m_OpponentPawnAttackMap{};

	Bitboard m_OpponentAttackMapNoPawns{};
	Bitboard m_OpponentKnightAttacks{};
	Bitboard m_OpponentSlidingAttackMap{};

	Bitboard m_FriendlyPawns{}; // check
	Bitboard m_FriendlyKinghts{}; // check
	Bitboard m_FriendlyKing{};// check

	Bitboard m_OpponentPawns{};// check
	Bitboard m_OpponentBishops{};// check
	Bitboard m_OpponentKnights{};// check
	Bitboard m_OpponentRooks{};// check
	Bitboard m_OpponentQueens{};// check
	
	Bitboard m_PinRays{}; // check
	Bitboard m_NotPinRays{};// check

	Bitboard m_CheckRayBitmask{}; // check

	bool m_InCheck{ false }; // check
	bool m_InDoubleCheck{ false }; // check

public:
	static constexpr uint8_t m_MaxPossibleMoves = 218;

	// --------- Members for Precomputing ---------

	// Mask for every square

	static Bitboard GetStraightSlidingMask(uint8_t square);
	static Bitboard GetDiagonalSlidingMask(uint8_t square);

	static std::array<Bitboard, 64> InitRookMasks();
	static std::array<Bitboard, 64> InitBishopMasks();

	static const std::array<Bitboard, 64> s_RookMasks;
	static const std::array<Bitboard, 64> s_BishopMasks;

	// Magic Shifts (Are known in advance)
	static const std::array<uint8_t, 64> s_RookShifts;
	static const std::array<uint8_t, 64> s_BishopShifts;

	// Magic Numbers (Are known in advance)
	static const std::array<uint64_t, 64> s_RookMagics;
	static const std::array<uint64_t, 64> s_BishopMagics;

	// Array Sizes
	static constexpr size_t s_MaxPossibleRookMasks = 4096;
	static constexpr size_t s_MaxPossibleBishopMasks = 512;

	static constexpr size_t s_RookMoveMaskSize = 64 * s_MaxPossibleRookMasks;
	static constexpr size_t s_BishopMoveMaskSize = 64 * s_MaxPossibleBishopMasks;

	// List of every possible move, based on index and square (square * s_MaxPossibleRook/BishopMasks + index)
	static std::unique_ptr<Bitboard[]> InitRookMoveMasks();
	static std::unique_ptr<Bitboard[]> InitBishopMoveMasks();

	static std::vector<Bitboard> CreateAllBlockerBitboards(Bitboard mask);

	static Bitboard CalculatePossibleRookMoves(uint8_t from_square, Bitboard blockers);
	static Bitboard CalculatePossibleBishopMoves(uint8_t from_square, Bitboard blockers);

	static const std::unique_ptr<Bitboard[]> s_RookMoveMasksArray;
	static const std::unique_ptr<Bitboard[]> s_BishopMoveMasksArray;

	// King Move Mask for every square
	static std::array<Bitboard, 64> InitKingMoveMask();
	static const std::array<Bitboard, 64> s_KingMoveMask;
	
	// Knight Move Mask for every square
	static std::array<Bitboard, 64> InitKnightMoveMask();
	static const std::array<Bitboard, 64> s_KnightMoveMask;

	// Pawn Attack Masks
	static std::array<Bitboard, 64> InitWhitePawnAttackMasks();
	static std::array<Bitboard, 64> InitBlackPawnAttackMasks();
	static const std::array<Bitboard, 64> s_WhitePawnAttackMasks;
	static const std::array<Bitboard, 64> s_BlackPawnAttackMasks;

	// Direction Offsets
	static const std::array<int, 8> s_DirectionOffsets;
	static const std::array<std::array<int, 2>, 8> s_DirectionOffsets2D;

	// Direction ray masks for sliding pieces
	static std::array<std::array<Bitboard, 64>, 8> InitDirRayMask();
	static const std::array<std::array<Bitboard, 64>, 8> s_DirRayMask;

	// Number of squares to edge in each direction
	static std::array<std::array<int, 8>, 64> InitSquaresToEdge();
	static const std::array<std::array<int, 8>, 64> s_SquaresToEdge;


	static const Bitboard s_WhiteKingsideMask;
	static const Bitboard s_BlackKingsideMask;

	static const Bitboard s_WhiteQueensideMask2;
	static const Bitboard s_BlackQueensideMask2;

	static const Bitboard s_WhiteQueensideMask;
	static const Bitboard s_BlackQueensideMask;

	// Align Mask for checking if a piece is aligned with the king
	static std::array<std::array<Bitboard, 64>, 64> InitAlignMask();
	static const std::array<std::array<Bitboard, 64>, 64> s_AlignMask;
};