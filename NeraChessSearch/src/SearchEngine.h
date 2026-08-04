#pragma once

#include "ChessBoard.h"
#include "TranspositionTable.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace NeraChessSearch
{
    using Score = int32_t;

    inline constexpr Score SCORE_INF = 32'000;
    inline constexpr Score SCORE_MATE = 30'000;
    inline constexpr Score SCORE_DRAW = 0;
    inline constexpr int MAX_PLY = 128;

    struct SearchLimits
    {
        int maxDepth = 64;
        uint64_t maxNodes = 0;
        std::chrono::milliseconds softTime{ 0 };
        std::chrono::milliseconds hardTime{ 0 };
    };

    struct SearchResult
    {
        NeraChessEngine::Move bestMove = 0;
        Score score = SCORE_DRAW;
        int completedDepth = 0;
        int selectiveDepth = 0;
        uint64_t nodes = 0;
        std::chrono::milliseconds elapsed{ 0 };
        std::vector<NeraChessEngine::Move> principalVariation;
        bool completed = false;
        bool aborted = false;
    };

    class SearchEngine
    {
    public:
        explicit SearchEngine(size_t hashMegabytes = 64);

        SearchResult Search(const NeraChessEngine::ChessBoard& position, const SearchLimits& limits);
        void RequestStop() { m_StopRequested = true; }
        void NewGame();
        void ResizeHash(size_t megabytes) { m_TranspositionTable.Resize(megabytes); }

        static Score Evaluate(const NeraChessEngine::ChessBoard& board);

    private:
        struct RootResult
        {
            NeraChessEngine::Move move = 0;
            Score score = -SCORE_INF;
        };

        RootResult SearchRoot(NeraChessEngine::ChessBoard& board, int depth, Score alpha, Score beta);
        Score PrincipalVariationSearch(NeraChessEngine::ChessBoard& board,
            Score alpha, Score beta, int depth, int ply, bool pvNode, bool allowNull);
        Score QuiescenceSearch(NeraChessEngine::ChessBoard& board, Score alpha, Score beta, int ply);

        void SortMoves(const NeraChessEngine::ChessBoard& board,
            NeraChessEngine::MoveList<218>& moves, int ply, NeraChessEngine::Move ttMove);
        void UpdatePrincipalVariation(int ply, NeraChessEngine::Move move);

        bool ShouldStop();
        bool IsDrawOrTerminal(const NeraChessEngine::ChessBoard& board, Score& terminalScore, int ply) const;
        static Score ScoreToTT(Score score, int ply);
        static Score ScoreFromTT(Score score, int ply);
        static int PieceValue(NeraChessEngine::Piece piece);
        static bool IsQuiet(NeraChessEngine::Move move);
        static bool IsFutilityPrunable(NeraChessEngine::Move move);
        static int LateMoveReduction(int depth, int moveIndex, bool pvNode);
        static bool HasNonPawnMaterial(const NeraChessEngine::ChessBoard& board);

    private:
        TranspositionTable m_TranspositionTable;
        SearchLimits m_Limits;
        std::chrono::steady_clock::time_point m_StartTime;
        std::atomic<bool> m_StopRequested{ false };
        bool m_Aborted = false;
        uint64_t m_Nodes = 0;
        int m_SelectiveDepth = 0;

        std::array<std::array<NeraChessEngine::Move, 2>, MAX_PLY> m_KillerMoves{};
        std::array<std::array<int32_t, 64>, 64> m_History{};
        std::array<std::array<NeraChessEngine::Move, MAX_PLY>, MAX_PLY> m_PvTable{};
        std::array<int, MAX_PLY> m_PvLength{};
    };
}
