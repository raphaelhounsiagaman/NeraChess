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

        Bitboard AttackersTo(uint8_t target, bool white, Bitboard occupancy,
            const std::array<Bitboard, 12>& pieces)
        {
            const uint8_t offset = white ? 0 : 6;
            const Bitboard pawnAttackers = white
                ? MoveGenerator::s_BlackPawnAttackMasks[target]
                : MoveGenerator::s_WhitePawnAttackMasks[target];
            const Bitboard diagonalAttacks = MoveGenerator::CalculatePossibleBishopMoves(target, occupancy);
            const Bitboard straightAttacks = MoveGenerator::CalculatePossibleRookMoves(target, occupancy);

            return (pawnAttackers & pieces[offset + PieceType::WHITE_PAWN]) |
                (MoveGenerator::s_KnightMoveMask[target] & pieces[offset + PieceType::WHITE_KNIGHT]) |
                (diagonalAttacks & (pieces[offset + PieceType::WHITE_BISHOP] |
                    pieces[offset + PieceType::WHITE_QUEEN])) |
                (straightAttacks & (pieces[offset + PieceType::WHITE_ROOK] |
                    pieces[offset + PieceType::WHITE_QUEEN])) |
                (MoveGenerator::s_KingMoveMask[target] & pieces[offset + PieceType::WHITE_KING]);
        }

        Attacker LeastValuableAttacker(uint8_t target, bool white, Bitboard occupancy,
            const std::array<Bitboard, 12>& pieces)
        {
            const uint8_t offset = white ? 0 : 6;
            const Bitboard allAttackers = AttackersTo(target, white, occupancy, pieces);
            for (uint8_t type = 0; type < 6; ++type)
            {
                Bitboard candidates = allAttackers & pieces[offset + type];
                if (!candidates)
                    continue;

                const uint8_t square = BitUtil::GetLSBIndex(candidates);
                if (type == PieceType::WHITE_KING)
                {
                    const Bitboard occupancyWithoutKing = occupancy & ~(1ULL << square);
                    if (AttackersTo(target, !white, occupancyWithoutKing, pieces))
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

    int StaticExchangeEvaluation(const ChessBoard& board, Move move)
    {
        const uint8_t flags = move.GetMoveFlags();
        const uint8_t target = move.GetTargetSquare();
        const uint8_t start = move.GetStartSquare();
        const uint8_t movingPiece = move.GetMovePiece();
        const bool whiteMoved = movingPiece < 6;

        std::array<Bitboard, 12> pieces = board.GetBoardState().pieceBitboards;
        Bitboard occupancy = Occupancy(pieces);

        uint8_t capturedPiece = PieceType::NO_PIECE;
        if (flags & MoveFlags::IS_EN_PASSANT)
        {
            capturedPiece = whiteMoved ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN;
            const uint8_t capturedSquare = whiteMoved ? target - 8 : target + 8;
            pieces[capturedPiece] &= ~(1ULL << capturedSquare);
            occupancy &= ~(1ULL << capturedSquare);
        }
        else if (flags & MoveFlags::IS_CAPTURE)
        {
            capturedPiece = board.GetPiece(target);
            pieces[capturedPiece] &= ~(1ULL << target);
        }

        pieces[movingPiece] &= ~(1ULL << start);
        occupancy &= ~(1ULL << start);
        occupancy |= 1ULL << target;

        uint8_t pieceOnTarget = movingPiece;
        int promotionGain = 0;
        if (flags & MoveFlags::IS_PROMOTION)
        {
            pieceOnTarget = move.GetPromoPiece();
            promotionGain = Value(pieceOnTarget) - Value(movingPiece);
        }

        std::array<int, 32> gains{};
        gains[0] = Value(capturedPiece) + promotionGain;
        int depth = 0;
        bool whiteToCapture = !whiteMoved;

        while (depth + 1 < static_cast<int>(gains.size()))
        {
            const Attacker attacker = LeastValuableAttacker(target, whiteToCapture, occupancy, pieces);
            if (!attacker)
                break;

            const int recapturePromotionGain = PromotesOn(attacker.piece, target)
                ? PieceValues[PieceType::WHITE_QUEEN] - PieceValues[PieceType::WHITE_PAWN]
                : 0;
            ++depth;
            gains[depth] = Value(pieceOnTarget) + recapturePromotionGain - gains[depth - 1];

            pieces[attacker.piece] &= ~(1ULL << attacker.square);
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
}
