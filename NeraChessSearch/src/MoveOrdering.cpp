#include "MoveOrdering.h"

#include "ChessUtil.h"
#include "MoveGenerator.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace NeraChessSearch::MoveOrdering
{
    using namespace NeraChessEngine;

    namespace
    {
        constexpr std::array<int, 6> PieceValues = { 100, 320, 330, 500, 900, 20'000 };

        struct Attacker
        {
            uint8_t piece = PieceType::NO_PIECE;
            uint8_t square = 0;

            explicit operator bool() const { return piece != PieceType::NO_PIECE; }
        };

        Bitboard Occupancy(const std::array<Bitboard, 12>& pieces)
        {
            Bitboard occupancy = 0;
            for (const Bitboard pieceSet : pieces)
                occupancy |= pieceSet;
            return occupancy;
        }

        // Pawn/knight/king attacks on `target` depend only on where those pieces started, not on
        // what's blocking anything, so unlike the sliding attacks below they never need to be
        // re-derived as the exchange proceeds -- computed once per colour and then just masked by
        // the shrinking `occupancy` to drop pieces already used as attackers.
        Bitboard NonSliderAttackers(uint8_t target, bool white, const std::array<Bitboard, 12>& pieces)
        {
            const uint8_t offset = white ? 0 : 6;
            const Bitboard pawnAttackers = white
                ? MoveGenerator::s_BlackPawnAttackMasks[target]
                : MoveGenerator::s_WhitePawnAttackMasks[target];
            return (pawnAttackers & pieces[offset + PieceType::WHITE_PAWN]) |
                (MoveGenerator::s_KnightMoveMask[target] & pieces[offset + PieceType::WHITE_KNIGHT]) |
                (MoveGenerator::s_KingMoveMask[target] & pieces[offset + PieceType::WHITE_KING]);
        }

        // `diagonalAttacks`/`straightAttacks` are the magic-table sliding attacks from `target`
        // against the caller's current `occupancy` -- the only part of the attacker set that a
        // vacated square can change, so the caller re-derives just those two lookups per exchange
        // step instead of the whole attacker set. `pieces` is never mutated as attackers are used;
        // masking every candidate by `occupancy` is what keeps a used piece's stale bitboard entry
        // from being picked again, without the cost of copying and clearing twelve bitboards.
        Attacker LeastValuableAttacker(uint8_t target, bool white, Bitboard occupancy,
            const std::array<Bitboard, 12>& pieces, Bitboard ownNonSliderAttackers,
            Bitboard opponentNonSliderAttackers, Bitboard diagonalAttacks, Bitboard straightAttacks)
        {
            const uint8_t offset = white ? 0 : 6;
            const Bitboard allAttackers = (ownNonSliderAttackers |
                (diagonalAttacks & (pieces[offset + PieceType::WHITE_BISHOP] |
                    pieces[offset + PieceType::WHITE_QUEEN])) |
                (straightAttacks & (pieces[offset + PieceType::WHITE_ROOK] |
                    pieces[offset + PieceType::WHITE_QUEEN]))) &
                occupancy;

            for (uint8_t type = 0; type < 6; ++type)
            {
                const Bitboard candidates = allAttackers & pieces[offset + type];
                if (!candidates)
                    continue;

                const uint8_t square = BitUtil::GetLSBIndex(candidates);
                if (type == PieceType::WHITE_KING)
                {
                    const Bitboard occupancyWithoutKing = occupancy & ~(1ULL << square);
                    const Bitboard oppositeDiagonal =
                        MoveGenerator::LookupBishopAttacks(target, occupancyWithoutKing);
                    const Bitboard oppositeStraight =
                        MoveGenerator::LookupRookAttacks(target, occupancyWithoutKing);
                    const uint8_t oppositeOffset = white ? 6 : 0;
                    const Bitboard opponentAttackers = (opponentNonSliderAttackers |
                        (oppositeDiagonal & (pieces[oppositeOffset + PieceType::WHITE_BISHOP] |
                            pieces[oppositeOffset + PieceType::WHITE_QUEEN])) |
                        (oppositeStraight & (pieces[oppositeOffset + PieceType::WHITE_ROOK] |
                            pieces[oppositeOffset + PieceType::WHITE_QUEEN]))) &
                        occupancyWithoutKing;
                    if (opponentAttackers)
                        return {};
                }
                return { static_cast<uint8_t>(offset + type), square };
            }
            return {};
        }

        int Value(uint8_t piece)
        {
            return piece == PieceType::NO_PIECE ? 0 : PieceValues[piece % 6];
        }

        bool PromotesOn(uint8_t piece, uint8_t target)
        {
            return (piece == PieceType::WHITE_PAWN && target >= 56) ||
                (piece == PieceType::BLACK_PAWN && target < 8);
        }
    }

    int StaticExchangeEvaluation(const ChessBoard& board, Move move, Piece capturedPiece)
    {
        const uint8_t flags = move.GetMoveFlags();
        const uint8_t target = move.GetTargetSquare();
        const uint8_t start = move.GetStartSquare();
        const uint8_t movingPiece = move.GetMovePiece();
        const bool whiteMoved = movingPiece < 6;

        // `pieces` is read-only for the whole exchange: only `occupancy` is mutated as attackers
        // are consumed, and every attacker lookup below masks by it, so a stale entry for an
        // already-used piece can never be picked again (see LeastValuableAttacker).
        const std::array<Bitboard, 12>& pieces = board.GetBoardState().pieceBitboards;
        Bitboard occupancy = Occupancy(pieces);

        if (flags & MoveFlags::IS_EN_PASSANT)
        {
            const uint8_t capturedSquare = whiteMoved ? target - 8 : target + 8;
            occupancy &= ~(1ULL << capturedSquare);
        }

        occupancy &= ~(1ULL << start);
        occupancy |= 1ULL << target;

        uint8_t pieceOnTarget = movingPiece;
        int promotionGain = 0;
        if (flags & MoveFlags::IS_PROMOTION)
        {
            pieceOnTarget = move.GetPromoPiece();
            promotionGain = Value(pieceOnTarget) - Value(movingPiece);
        }

        // Every index this function reads was written just above or in the loop below -- gains[0]
        // here, gains[1..depth] in the loop -- so unlike a member array reused across calls, this
        // one doesn't need to start zeroed.
        std::array<int, 32> gains;
        gains[0] = Value(capturedPiece) + promotionGain;
        int depth = 0;
        bool whiteToCapture = !whiteMoved;

        const Bitboard nonSliderWhite = NonSliderAttackers(target, true, pieces);
        const Bitboard nonSliderBlack = NonSliderAttackers(target, false, pieces);

        while (depth + 1 < static_cast<int>(gains.size()))
        {
            const Bitboard diagonalAttacks = MoveGenerator::LookupBishopAttacks(target, occupancy);
            const Bitboard straightAttacks = MoveGenerator::LookupRookAttacks(target, occupancy);
            const Attacker attacker = whiteToCapture
                ? LeastValuableAttacker(target, true, occupancy, pieces, nonSliderWhite,
                    nonSliderBlack, diagonalAttacks, straightAttacks)
                : LeastValuableAttacker(target, false, occupancy, pieces, nonSliderBlack,
                    nonSliderWhite, diagonalAttacks, straightAttacks);
            if (!attacker)
                break;

            const int recapturePromotionGain = PromotesOn(attacker.piece, target)
                ? PieceValues[PieceType::WHITE_QUEEN] - PieceValues[PieceType::WHITE_PAWN]
                : 0;
            ++depth;
            gains[depth] = Value(pieceOnTarget) + recapturePromotionGain - gains[depth - 1];

            occupancy &= ~(1ULL << attacker.square);
            pieceOnTarget = recapturePromotionGain == 0
                ? attacker.piece
                : static_cast<uint8_t>((attacker.piece < 6 ? 0 : 6) + PieceType::WHITE_QUEEN);
            whiteToCapture = !whiteToCapture;
        }

        while (depth > 0)
        {
            gains[depth - 1] = -std::max(-gains[depth - 1], gains[depth]);
            --depth;
        }
        return gains[0];
    }

    int StaticExchangeEvaluation(const ChessBoard& board, Move move)
    {
        const uint8_t flags = move.GetMoveFlags();
        const uint8_t target = move.GetTargetSquare();
        const uint8_t movingPiece = move.GetMovePiece();

        const Piece capturedPiece = (flags & MoveFlags::IS_EN_PASSANT)
            ? Piece(movingPiece < 6 ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN)
            : board.GetPiece(target);
        return StaticExchangeEvaluation(board, move, capturedPiece);
    }
}
