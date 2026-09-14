#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChessUtil.h"
#include "Piece.h"
#include "Square.h"
#include "Move.h"
#include "Undo.h"
#include "MoveList.h"
#include "BoardState.h"
#include "RepetitionTable.h"
#include "MoveGenerator.h"


namespace NeraChessEngine
{
    using  Bitboard = uint64_t;

    enum  GameOverFlags : uint16_t
    {
        IS_GAME_CONTINUE = 0,

        IS_GAME_OVER = 1 << 0, // Bit 1

        IS_WHITE_WIN = 1 << 1, // Bit 2
	    IS_DRAW = 1 << 2, // Bit 3

        IS_CHECKMATE = 1 << 3, // Bit 4
        IS_RESIGN =  1 << 4, // Bit 5
        IS_TIMEOUT = 1 << 5, // Bit 6

        IS_STALEMATE = 1 << 6, // Bit 7
        IS_REPETITION = 1 << 7, // Bit 8
        IS_50MOVE_RULE = 1 << 8, // Bit 9
        IS_INSUFFICIENT_MATERIAL = 1 << 9, // Bit 10
        IS_AGREE_ON_DRAW = 1 << 10, // Bit 11
    };

    class ChessBoard
    {
    public:
	    ChessBoard(const std::string& fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	    ~ChessBoard() = default;

        MoveList<218> GetLegalMoves() const;

	    // Same legal moves as GetLegalMoves(), without the ~880-byte copy -- for
	    // callers that only read the list (e.g. filtering into a shorter list) and
	    // never hold onto it past the next move made on this board. Backed directly
	    // by the MoveGenerator's own list -- ChessBoard keeps no second copy.
	    const MoveList<218>& GetLegalMovesRef() const;

	    // Captures, capture-promotions, quiet promotions and en passant only --
	    // for callers (quiescence) that only ever want the subset GetLegalMovesRef()
	    // would otherwise be filtered down to. Must only be called when the side to
	    // move is not in check: unlike GetLegalMovesRef(), an empty result here does
	    // not distinguish "no captures available" from stalemate, and it does not
	    // produce check evasions. Backed by a generator separate from
	    // GetLegalMovesRef()'s so neither call invalidates the other's cached list.
	    const MoveList<218>& GetCapturesOnlyMovesRef() const;

        void MakeMove(Move move, bool gameMove = false);
	    void UndoMove(Move move);

	    bool MakeNullMove();
	    void UndoNullMove();

	    const BoardState& GetBoardState() const { return m_BoardState; }

		    uint16_t GetHalfMoveClock() const{ return m_HalfMoveClock; }
	    uint16_t GetFullMoveClock() const { return m_FullMoves; }

        Piece GetPiece(const uint8_t square) const;

	    // Attack-table lookup: whether the side to move's king is attacked, without
	    // generating the legal move list. Existing callers relied on IsInCheck() to
	    // force move generation as a side effect; none of them need that list at the
	    // point they call it, so this is a strict improvement rather than a behavior
	    // change.
        bool IsInCheck() const;

	    // Reference implementation of IsInCheck(), kept only so the attack-table
	    // version above can be verified against it exhaustively in tests.
	    bool IsInCheckByMoveGeneration() const;

	    // Reports whether a legal move would check the opponent, without making it.
	    // Making the move and calling IsInCheck() answers the same question but forces
	    // a full legal-move generation for the child position, which is wasted work for
	    // a move the search is about to discard.
	    //
	    // Fast path: every input to the answer except `move` is fixed for the whole
	    // node, so a per-node cache of "which squares would give check" turns most
	    // calls into a couple of bitboard tests. The cache is a one-sided filter --
	    // it only ever proves a move does NOT give check; anything it cannot rule
	    // out (including every real check) falls back to GivesCheckSlow() for the
	    // exact answer, so a stale or imprecise cache can only cost performance,
	    // never correctness.
	    bool GivesCheck(Move move) const;

        uint16_t GetGameOver(bool gameCheck = true) const;

	    // Reports the draw conditions that do not require a legal move list: the
	    // 50-move rule, threefold repetition, and insufficient material. Does not
	    // detect checkmate or stalemate -- a caller needing those still has to
	    // generate moves and check for an empty list.
	    bool IsRuleDraw() const;

	    // Number of positions recorded for repetition detection so far (see
	    // RepetitionTable::Size). Search uses this, rather than its own ply
	    // counter, to measure distance back to its root.
	    std::size_t GetRepetitionPlies() const;

	    // Whether the current position has occurred at least once more within
	    // `distance` plies before it (same side to move, i.e. every second ply),
	    // counting the current position itself. This is the in-tree "twofold"
	    // test the search uses to score a repetition as a draw before a
	    // game-rule threefold (see GetGameOver) would fire.
	    bool RepeatsWithin(std::size_t distance) const;

        uint64_t GetZobristKey() const;
		uint8_t GetZobristEnPassantFile() const;
        std::string GetFENString() const;

        uint8_t GetError() const { return m_Error; }

        bool operator==(const ChessBoard& other) const;

	    private:

        static uint64_t PerfTest(int depth, ChessBoard& board);

	    static bool InsufficentMaterial(const ChessBoard& board);
	    uint64_t GetRepetitionKey() const;

	    // Exact GivesCheck: rebuilds the post-move attack picture from scratch.
	    // Used directly for castling/en-passant (which relocate a second piece
	    // outside the landed-piece model below) and as the fallback whenever the
	    // per-node check-square cache cannot rule a move out.
	    bool GivesCheckSlow(Move move) const;

	    // Builds m_CheckSquares/m_DiscoveryRing/m_NoEnemyKing for the current
	    // position if m_CheckInfoValid is false. Call before reading any of them.
	    void EnsureCheckInfo() const;

    private:

        mutable MoveGenerator m_MoveGenerator;

	    // Separate from m_MoveGenerator so GetCapturesOnlyMovesRef() never
	    // overwrites the cached full legal-move list (or vice versa) -- it is
	    // always regenerated fresh, so it needs no m_WasBoardStateChanged-style cache.
	    mutable MoveGenerator m_CapturesMoveGenerator;

	    mutable bool m_WasBoardStateChanged = true;

	    // Per-node cache for GivesCheck's fast reject. Indexed by PieceType % 6
	    // (landed piece type); m_CheckSquares[type] holds the squares from which a
	    // piece of that type would attack the enemy king given the position's
	    // current occupancy. m_DiscoveryRing is a superset of the squares whose
	    // vacation could open a discovered check (the first occupied square on
	    // each ray from the enemy king). Invalidated at the same points as
	    // m_WasBoardStateChanged, but with its own flag: that one is also cleared
	    // by move generation as a side effect, which this cache must not be.
	    mutable bool m_CheckInfoValid = false;
	    mutable Bitboard m_CheckSquares[6] = {};
	    mutable Bitboard m_DiscoveryRing = 0;
	    mutable bool m_NoEnemyKing = false;

	    mutable uint64_t m_ZobristKey = 0;
        mutable bool m_ZobristKeySet = true;

        mutable uint16_t m_GameOverFlags = 0;

        BoardState m_BoardState{};

	    RepetitionTable m_RepetitionTable{};
	    UndoStack m_UndoStack{};

        std::vector<Move> m_MovesPlayed{};

		    uint16_t m_HalfMoveClock = 0;
        uint16_t m_FullMoves = 1;

        static constexpr Bitboard s_SquareBitboard[64] = {
            1ULL <<  0, 1ULL <<  1, 1ULL <<  2, 1ULL <<  3, 1ULL <<  4, 1ULL <<  5, 1ULL <<  6, 1ULL <<  7,
            1ULL <<  8, 1ULL <<  9, 1ULL << 10, 1ULL << 11, 1ULL << 12, 1ULL << 13, 1ULL << 14, 1ULL << 15,
            1ULL << 16, 1ULL << 17, 1ULL << 18, 1ULL << 19, 1ULL << 20, 1ULL << 21, 1ULL << 22, 1ULL << 23,
            1ULL << 24, 1ULL << 25, 1ULL << 26, 1ULL << 27, 1ULL << 28, 1ULL << 29, 1ULL << 30, 1ULL << 31,
            1ULL << 32, 1ULL << 33, 1ULL << 34, 1ULL << 35, 1ULL << 36, 1ULL << 37, 1ULL << 38, 1ULL << 39,
            1ULL << 40, 1ULL << 41, 1ULL << 42, 1ULL << 43, 1ULL << 44, 1ULL << 45, 1ULL << 46, 1ULL << 47,
            1ULL << 48, 1ULL << 49, 1ULL << 50, 1ULL << 51, 1ULL << 52, 1ULL << 53, 1ULL << 54, 1ULL << 55,
            1ULL << 56, 1ULL << 57, 1ULL << 58, 1ULL << 59, 1ULL << 60, 1ULL << 61, 1ULL << 62, 1ULL << 63
        };

        uint8_t m_Error = 0;
    };

} // namespace NeraChessEngine
