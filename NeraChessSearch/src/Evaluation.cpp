#include "Evaluation.h"

#include "ChessUtil.h"
#include "MoveGenerator.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace NeraChessSearch::Evaluation
{
    using namespace NeraChessEngine;

    namespace
    {
        constexpr int MaxPhase = 24;
        constexpr int TempoBonus = 10;

        constexpr std::array<int, 6> MiddleGameValues = { 82, 337, 365, 477, 1025, 0 };
        constexpr std::array<int, 6> EndGameValues = { 94, 281, 297, 512, 936, 0 };
        constexpr std::array<int, 6> PhaseWeights = { 0, 1, 1, 2, 4, 0 };
        constexpr std::array<int, 8> MiddleGamePassedPawn = { 0, 0, 5, 12, 25, 45, 80, 0 };
        constexpr std::array<int, 8> EndGamePassedPawn = { 0, 5, 12, 25, 45, 80, 140, 0 };

        constexpr std::array<Bitboard, 8> BuildFileMasks()
        {
            std::array<Bitboard, 8> masks{};
            for (uint8_t file = 0; file < 8; ++file)
            {
                for (uint8_t rank = 0; rank < 8; ++rank)
                    masks[file] |= 1ULL << (rank * 8 + file);
            }
            return masks;
        }

        constexpr std::array<Bitboard, 8> FileMasks = BuildFileMasks();

        struct SideEvaluation
        {
            int middleGame = 0;
            int endGame = 0;
            Bitboard attacks = 0;
        };

        constexpr std::array<std::array<int16_t, 64>, 6> MiddleGameTables = {{
            {{
                  0,   0,   0,   0,   0,   0,   0,   0,
                 98, 134,  61,  95,  68, 126,  34, -11,
                 -6,   7,  26,  31,  65,  56,  25, -20,
                -14,  13,   6,  21,  23,  12,  17, -23,
                -27,  -2,  -5,  12,  17,   6,  10, -25,
                -26,  -4,  -4, -10,   3,   3,  33, -12,
                -35,  -1, -20, -23, -15,  24,  38, -22,
                  0,   0,   0,   0,   0,   0,   0,   0,
            }},
            {{
                -167, -89, -34, -49,  61, -97, -15, -107,
                 -73, -41,  72,  36,  23,  62,   7,  -17,
                 -47,  60,  37,  65,  84, 129,  73,   44,
                  -9,  17,  19,  53,  37,  69,  18,   22,
                 -13,   4,  16,  13,  28,  19,  21,   -8,
                 -23,  -9,  12,  10,  19,  17,  25,  -16,
                 -29, -53, -12,  -3,  -1,  18, -14,  -19,
                -105, -21, -58, -33, -17, -28, -19,  -23,
            }},
            {{
                -29,   4, -82, -37, -25, -42,   7,  -8,
                -26,  16, -18, -13,  30,  59,  18, -47,
                -16,  37,  43,  40,  35,  50,  37,  -2,
                 -4,   5,  19,  50,  37,  37,   7,  -2,
                 -6,  13,  13,  26,  34,  12,  10,   4,
                  0,  15,  15,  15,  14,  27,  18,  10,
                  4,  15,  16,   0,   7,  21,  33,   1,
                -33,  -3, -14, -21, -13, -12, -39, -21,
            }},
            {{
                 32,  42,  32,  51,  63,   9,  31,  43,
                 27,  32,  58,  62,  80,  67,  26,  44,
                 -5,  19,  26,  36,  17,  45,  61,  16,
                -24, -11,   7,  26,  24,  35,  -8, -20,
                -36, -26, -12,  -1,   9,  -7,   6, -23,
                -45, -25, -16, -17,   3,   0,  -5, -33,
                -44, -16, -20,  -9,  -1,  11,  -6, -71,
                -19, -13,   1,  17,  16,   7, -37, -26,
            }},
            {{
                -28,   0,  29,  12,  59,  44,  43,  45,
                -24, -39,  -5,   1, -16,  57,  28,  54,
                -13, -17,   7,   8,  29,  56,  47,  57,
                -27, -27, -16, -16,  -1,  17,  -2,   1,
                 -9, -26,  -9, -10,  -2,  -4,   3,  -3,
                -14,   2, -11,  -2,  -5,   2,  14,   5,
                -35,  -8,  11,   2,   8,  15,  -3,   1,
                 -1, -18,  -9,  10, -15, -25, -31, -50,
            }},
            {{
                -65,  23,  16, -15, -56, -34,   2,  13,
                 29,  -1, -20,  -7,  -8,  -4, -38, -29,
                 -9,  24,   2, -16, -20,   6,  22, -22,
                -17, -20, -12, -27, -30, -25, -14, -36,
                -49,  -1, -27, -39, -46, -44, -33, -51,
                -14, -14, -22, -46, -44, -30, -15, -27,
                  1,   7,  -8, -64, -43, -16,   9,   8,
                -15,  36,  12, -54,   8, -28,  24,  14,
            }},
        }};

        constexpr std::array<std::array<int16_t, 64>, 6> EndGameTables = {{
            {{
                  0,   0,   0,   0,   0,   0,   0,   0,
                178, 173, 158, 134, 147, 132, 165, 187,
                 94, 100,  85,  67,  56,  53,  82,  84,
                 32,  24,  13,   5,  -2,   4,  17,  17,
                 13,   9,  -3,  -7,  -7,  -8,   3,  -1,
                  4,   7,  -6,   1,   0,  -5,  -1,  -8,
                 13,   8,   8,  10,  13,   0,   2,  -7,
                  0,   0,   0,   0,   0,   0,   0,   0,
            }},
            {{
                -58, -38, -13, -28, -31, -27, -63, -99,
                -25,  -8, -25,  -2,  -9, -25, -24, -52,
                -24, -20,  10,   9,  -1,  -9, -19, -41,
                -17,   3,  22,  22,  22,  11,   8, -18,
                -18,  -6,  16,  25,  16,  17,   4, -18,
                -23,  -3,  -1,  15,  10,  -3, -20, -22,
                -42, -20, -10,  -5,  -2, -20, -23, -44,
                -29, -51, -23, -15, -22, -18, -50, -64,
            }},
            {{
                -14, -21, -11,  -8,  -7,  -9, -17, -24,
                 -8,  -4,   7, -12,  -3, -13,  -4, -14,
                  2,  -8,   0,  -1,  -2,   6,   0,   4,
                 -3,   9,  12,   9,  14,  10,   3,   2,
                 -6,   3,  13,  19,   7,  10,  -3,  -9,
                -12,  -3,   8,  10,  13,   3,  -7, -15,
                -14, -18,  -7,  -1,   4,  -9, -15, -27,
                -23,  -9, -23,  -5,  -9, -16,  -5, -17,
            }},
            {{
                 13,  10,  18,  15,  12,  12,   8,   5,
                 11,  13,  13,  11,  -3,   3,   8,   3,
                  7,   7,   7,   5,   4,  -3,  -5,  -3,
                  4,   3,  13,   1,   2,   1,  -1,   2,
                  3,   5,   8,   4,  -5,  -6,  -8, -11,
                 -4,   0,  -5,  -1,  -7, -12,  -8, -16,
                 -6,  -6,   0,   2,  -9,  -9, -11,  -3,
                 -9,   2,   3,  -1,  -5, -13,   4, -20,
            }},
            {{
                 -9,  22,  22,  27,  27,  19,  10,  20,
                -17,  20,  32,  41,  58,  25,  30,   0,
                -20,   6,   9,  49,  47,  35,  19,   9,
                  3,  22,  24,  45,  57,  40,  57,  36,
                -18,  28,  19,  47,  31,  34,  39,  23,
                -16, -27,  15,   6,   9,  17,  10,   5,
                -22, -23, -30, -16, -16, -23, -36, -32,
                -33, -28, -22, -43,  -5, -32, -20, -41,
            }},
            {{
                -74, -35, -18, -18, -11,  15,   4, -17,
                -12,  17,  14,  17,  17,  38,  23,  11,
                 10,  17,  23,  15,  20,  45,  44,  13,
                 -8,  22,  24,  27,  26,  33,  26,   3,
                -18,  -4,  21,  24,  27,  23,   9, -11,
                -19,  -3,  11,  21,  23,  16,   7,  -9,
                -27, -11,   4,  13,  14,   4,  -5, -17,
                -53, -34, -21, -11, -28, -14, -24, -43,
            }},
        }};

        Bitboard SliderAttacks(uint8_t square, Bitboard occupancy, bool diagonal)
        {
            if (diagonal)
            {
                const Bitboard blockers = occupancy & MoveGenerator::s_BishopMasks[square];
                const uint64_t index = static_cast<uint64_t>(square) *
                    MoveGenerator::s_MaxPossibleBishopMasks +
                    ((blockers * MoveGenerator::s_BishopMagics[square]) >>
                        MoveGenerator::s_BishopShifts[square]);
                return MoveGenerator::s_BishopMoveMasksArray[index];
            }

            const Bitboard blockers = occupancy & MoveGenerator::s_RookMasks[square];
            const uint64_t index = static_cast<uint64_t>(square) *
                MoveGenerator::s_MaxPossibleRookMasks +
                ((blockers * MoveGenerator::s_RookMagics[square]) >>
                    MoveGenerator::s_RookShifts[square]);
            return MoveGenerator::s_RookMoveMasksArray[index];
        }

        bool IsPassedPawn(uint8_t square, bool white, Bitboard enemyPawns)
        {
            const int file = square % 8;
            const int direction = white ? 1 : -1;
            for (int rank = square / 8 + direction; rank >= 0 && rank < 8; rank += direction)
            {
                for (int candidateFile = std::max(0, file - 1);
                    candidateFile <= std::min(7, file + 1); ++candidateFile)
                {
                    if (enemyPawns & (1ULL << (rank * 8 + candidateFile)))
                        return false;
                }
            }
            return true;
        }

        SideEvaluation EvaluateSide(const BoardState& state, bool white, Bitboard occupancy)
        {
            const uint8_t offset = white ? 0 : 6;
            const uint8_t enemyOffset = white ? 6 : 0;
            const Bitboard pawns = state.pieceBitboards[offset + PieceType::WHITE_PAWN];
            const Bitboard enemyPawns = state.pieceBitboards[enemyOffset + PieceType::WHITE_PAWN];
            Bitboard friendlies = 0;
            for (uint8_t type = 0; type < 6; ++type)
                friendlies |= state.pieceBitboards[offset + type];

            SideEvaluation result;
            for (uint8_t file = 0; file < 8; ++file)
            {
                const int count = BitUtil::PopCnt(pawns & FileMasks[file]);
                if (count > 1)
                {
                    result.middleGame -= (count - 1) * 10;
                    result.endGame -= (count - 1) * 15;
                }
            }

            Bitboard remainingPawns = pawns;
            while (remainingPawns)
            {
                const uint8_t square = BitUtil::PopLSB(remainingPawns);
                const uint8_t file = square % 8;
                const int relativeRank = white ? square / 8 : 7 - square / 8;
                const Bitboard adjacentFiles = (file > 0 ? FileMasks[file - 1] : 0) |
                    (file < 7 ? FileMasks[file + 1] : 0);
                if (!(pawns & adjacentFiles))
                {
                    result.middleGame -= 12;
                    result.endGame -= 10;
                }
                if (IsPassedPawn(square, white, enemyPawns))
                {
                    result.middleGame += MiddleGamePassedPawn[relativeRank];
                    result.endGame += EndGamePassedPawn[relativeRank];
                }

                const Bitboard pawnAttacks = white
                    ? MoveGenerator::s_WhitePawnAttackMasks[square]
                    : MoveGenerator::s_BlackPawnAttackMasks[square];
                result.attacks |= pawnAttacks;
                const Bitboard protectors = white
                    ? MoveGenerator::s_BlackPawnAttackMasks[square]
                    : MoveGenerator::s_WhitePawnAttackMasks[square];
                if (protectors & pawns)
                {
                    result.middleGame += 5;
                    result.endGame += 8;
                }
            }

            Bitboard knights = state.pieceBitboards[offset + PieceType::WHITE_KNIGHT];
            while (knights)
            {
                const uint8_t square = BitUtil::PopLSB(knights);
                const Bitboard attacks = MoveGenerator::s_KnightMoveMask[square];
                result.attacks |= attacks;
                const int mobility = BitUtil::PopCnt(attacks & ~friendlies);
                result.middleGame += mobility * 4;
                result.endGame += mobility * 4;
            }

            Bitboard bishops = state.pieceBitboards[offset + PieceType::WHITE_BISHOP];
            if (BitUtil::PopCnt(bishops) >= 2)
            {
                result.middleGame += 28;
                result.endGame += 40;
            }
            while (bishops)
            {
                const uint8_t square = BitUtil::PopLSB(bishops);
                const Bitboard attacks = SliderAttacks(square, occupancy, true);
                result.attacks |= attacks;
                const int mobility = BitUtil::PopCnt(attacks & ~friendlies);
                result.middleGame += mobility * 4;
                result.endGame += mobility * 5;
            }

            Bitboard rooks = state.pieceBitboards[offset + PieceType::WHITE_ROOK];
            while (rooks)
            {
                const uint8_t square = BitUtil::PopLSB(rooks);
                const Bitboard attacks = SliderAttacks(square, occupancy, false);
                result.attacks |= attacks;
                const int mobility = BitUtil::PopCnt(attacks & ~friendlies);
                result.middleGame += mobility * 2;
                result.endGame += mobility * 3;

                const uint8_t file = square % 8;
                if (!(pawns & FileMasks[file]))
                {
                    result.middleGame += 10;
                    result.endGame += 6;
                    if (!(enemyPawns & FileMasks[file]))
                    {
                        result.middleGame += 10;
                        result.endGame += 6;
                    }
                }
            }

            Bitboard queens = state.pieceBitboards[offset + PieceType::WHITE_QUEEN];
            while (queens)
            {
                const uint8_t square = BitUtil::PopLSB(queens);
                const Bitboard attacks = SliderAttacks(square, occupancy, true) |
                    SliderAttacks(square, occupancy, false);
                result.attacks |= attacks;
                const int mobility = BitUtil::PopCnt(attacks & ~friendlies);
                result.middleGame += mobility;
                result.endGame += mobility * 2;
            }

            const Bitboard king = state.pieceBitboards[offset + PieceType::WHITE_KING];
            if (king)
            {
                const uint8_t kingSquare = BitUtil::GetLSBIndex(king);
                result.attacks |= MoveGenerator::s_KingMoveMask[kingSquare];
                const int kingRank = kingSquare / 8;
                const int shieldRank = kingRank + (white ? 1 : -1);
                if (shieldRank >= 0 && shieldRank < 8)
                {
                    for (int file = std::max(0, static_cast<int>(kingSquare % 8) - 1);
                        file <= std::min(7, static_cast<int>(kingSquare % 8) + 1); ++file)
                    {
                        if (pawns & (1ULL << (shieldRank * 8 + file)))
                            result.middleGame += 7;
                    }
                }

                for (int file = std::max(0, static_cast<int>(kingSquare % 8) - 1);
                    file <= std::min(7, static_cast<int>(kingSquare % 8) + 1); ++file)
                {
                    if (!(pawns & FileMasks[file]))
                        result.middleGame -= 6;
                }
            }
            return result;
        }
    }

    int32_t Evaluate(const ChessBoard& board)
    {
        int middleGameScore = 0;
        int endGameScore = 0;
        int phase = 0;

        const BoardState& state = board.GetBoardState();
        Bitboard occupancy = 0;
        for (uint8_t piece = 0; piece < 12; ++piece)
        {
            Bitboard pieces = state.pieceBitboards[piece];
            occupancy |= pieces;
            const int type = piece % 6;
            const bool white = piece < 6;
            phase += PhaseWeights[type] * BitUtil::PopCnt(pieces);

            while (pieces)
            {
                const uint8_t square = BitUtil::PopLSB(pieces);
                const uint8_t tableSquare = white ? square ^ 56 : square;
                const int sign = white ? 1 : -1;
                middleGameScore += sign * (MiddleGameValues[type] + MiddleGameTables[type][tableSquare]);
                endGameScore += sign * (EndGameValues[type] + EndGameTables[type][tableSquare]);
            }
        }

        SideEvaluation white = EvaluateSide(state, true, occupancy);
        SideEvaluation black = EvaluateSide(state, false, occupancy);
        const Bitboard whiteKing = state.pieceBitboards[PieceType::WHITE_KING];
        const Bitboard blackKing = state.pieceBitboards[PieceType::BLACK_KING];
        if (whiteKing)
        {
            const uint8_t square = BitUtil::GetLSBIndex(whiteKing);
            const int pressure = BitUtil::PopCnt(black.attacks & MoveGenerator::s_KingMoveMask[square]);
            white.middleGame -= pressure * 6;
            white.endGame -= pressure * 2;
        }
        if (blackKing)
        {
            const uint8_t square = BitUtil::GetLSBIndex(blackKing);
            const int pressure = BitUtil::PopCnt(white.attacks & MoveGenerator::s_KingMoveMask[square]);
            black.middleGame -= pressure * 6;
            black.endGame -= pressure * 2;
        }
        middleGameScore += white.middleGame - black.middleGame;
        endGameScore += white.endGame - black.endGame;

        phase = std::min(phase, MaxPhase);
        const int taperedScore = (middleGameScore * phase + endGameScore * (MaxPhase - phase)) / MaxPhase;
        const int sideToMoveScore = state.HasFlag(BoardStateFlags::WhiteToMove)
            ? taperedScore : -taperedScore;
        return sideToMoveScore + TempoBonus;
    }
}
