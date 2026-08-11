#pragma once

#include "Accumulator.h"
#include "ChessBoard.h"
#include "Network.h"
#include "TranspositionTable.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace NeraChessSearch
{
    using Score = int32_t;

    inline constexpr Score SCORE_INF = 32'000;
    inline constexpr Score SCORE_MATE = 30'000;
    inline constexpr Score SCORE_DRAW = 0;
    inline constexpr int MAX_PLY = 128;

    struct SearchResult
    {
        NeraChessEngine::Move bestMove = 0;
        Score score = SCORE_DRAW;
        int completedDepth = 0;
        int selectiveDepth = 0;
        uint64_t nodes = 0;
        int hashFullPermill = 0;
        std::chrono::milliseconds elapsed{ 0 };
        std::vector<NeraChessEngine::Move> principalVariation;
        bool completed = false;
        bool aborted = false;
    };

    struct SearchLimits
    {
        int maxDepth = 64;
        uint64_t maxNodes = 0;
        std::chrono::milliseconds softTime{ 0 };
        std::chrono::milliseconds hardTime{ 0 };
        std::vector<NeraChessEngine::Move> rootMoves;
        std::function<void(const SearchResult&)> iterationCallback;
    };

    class SearchEngine
    {
    public:
        explicit SearchEngine(size_t hashMegabytes = 64);

        SearchResult Search(const NeraChessEngine::ChessBoard& position, const SearchLimits& limits);
        void RequestStop() { m_SharedState->stopRequested = true; }
        void PrepareSearch(bool pondering = false);
        void PonderHit();
        void NewGame();
        void ResizeHash(size_t megabytes);
        void SetThreadCount(size_t threadCount);
        size_t GetThreadCount() const { return m_ThreadCount; }

        // Standalone evaluation for callers outside a search, such as the UCI
        // "eval" command and the regression tests. Refreshes a scratch
        // accumulator rather than using the search's stack.
        static Score Evaluate(const NeraChessEngine::ChessBoard& board);

    private:
        struct SharedSearchState
        {
            std::atomic<bool> stopRequested{ false };
            std::atomic<bool> searchStopped{ false };
            std::atomic<int64_t> timeControlStartMilliseconds{ 0 };
            std::atomic<uint64_t> nodes{ 0 };
        };

        struct RootResult
        {
            NeraChessEngine::Move move = 0;
            Score score = -SCORE_INF;
        };

        SearchEngine(size_t workerIndex,
            std::shared_ptr<TranspositionTable> transpositionTable,
            std::shared_ptr<SharedSearchState> sharedState);
        SearchResult SearchWorker(const NeraChessEngine::ChessBoard& position,
            const SearchLimits& limits);
        RootResult SearchRoot(NeraChessEngine::ChessBoard& board, int depth, Score alpha, Score beta);
        Score PrincipalVariationSearch(NeraChessEngine::ChessBoard& board,
            Score alpha, Score beta, int depth, int ply, bool pvNode, bool allowNull,
            NeraChessEngine::Move previousMove);
        Score QuiescenceSearch(NeraChessEngine::ChessBoard& board, Score alpha, Score beta, int ply);

        void SortMoves(const NeraChessEngine::ChessBoard& board,
            NeraChessEngine::MoveList<218>& moves, int ply, NeraChessEngine::Move ttMove,
            NeraChessEngine::Move previousMove = 0);
        // Evaluation of the node the search is standing on, using this
        // worker's accumulator stack.
        Score EvaluateNode(const NeraChessEngine::ChessBoard& board);

        // Make and unmake, paired with the accumulator stack. Every move the
        // search plays goes through these two so the accumulator cannot drift
        // out of step with the board.
        void MakeSearchMove(NeraChessEngine::ChessBoard& board, NeraChessEngine::Move move);
        void UndoSearchMove(NeraChessEngine::ChessBoard& board, NeraChessEngine::Move move);

        void UpdatePrincipalVariation(int ply, NeraChessEngine::Move move);
        void ResetHeuristics();
        void CountNode();
        void FlushNodeCount();
        uint64_t AggregateNodeCount() const;

        bool ShouldStop();
        bool IsDrawOrTerminal(const NeraChessEngine::ChessBoard& board, Score& terminalScore, int ply) const;
        static Score ScoreToTT(Score score, int ply);
        static Score ScoreFromTT(Score score, int ply);
        static int PieceValue(NeraChessEngine::Piece piece);
        static bool IsQuiet(NeraChessEngine::Move move);
        static bool IsFutilityPrunable(NeraChessEngine::Move move);
        static int LateMoveReduction(int depth, int moveIndex, bool pvNode);
        static bool HasNonPawnMaterial(const NeraChessEngine::ChessBoard& board);
        bool IsRootMoveAllowed(NeraChessEngine::Move move) const;
        NeraChessEngine::Move GetCounterMove(NeraChessEngine::Move previousMove) const;
        static void UpdateHistoryScore(int32_t& score, int bonus);
        std::chrono::milliseconds TimeControlElapsed() const;
        static int64_t SteadyMilliseconds();

    private:
        std::shared_ptr<TranspositionTable> m_TranspositionTable;
        std::shared_ptr<SharedSearchState> m_SharedState;
        std::vector<std::unique_ptr<SearchEngine>> m_HelperEngines;
        size_t m_ThreadCount = 1;
        size_t m_WorkerIndex = 0;
        SearchLimits m_Limits;
        std::chrono::steady_clock::time_point m_StartTime;
        std::atomic<bool> m_TimeControlPrepared{ false };
        bool m_Aborted = false;
        uint64_t m_Nodes = 0;
        uint64_t m_UnflushedNodes = 0;
        int m_SelectiveDepth = 0;

        // One NNUE accumulator per ply, private to this worker, updated
        // incrementally as the search makes and unmakes moves.
        NeraChessNNUE::AccumulatorStack m_Accumulators{};

        // The network in use for the current search, cached so the per-move
        // path does not re-resolve it. Immutable while a search runs.
        const NeraChessNNUE::Network* m_Network = nullptr;

        std::array<std::array<NeraChessEngine::Move, 2>, MAX_PLY> m_KillerMoves{};
        std::array<std::array<std::array<int32_t, 64>, 64>, 2> m_History{};
        std::array<std::array<NeraChessEngine::Move, 64>, 12> m_CounterMoves{};
        std::array<std::array<NeraChessEngine::Move, MAX_PLY>, MAX_PLY> m_PvTable{};
        std::array<int, MAX_PLY> m_PvLength{};
    };

    // Callers routinely construct a SearchEngine inside a worker thread, and a
    // thread gets a 512 KB stack by default on macOS. Anything that pushes this
    // type past that overflows the stack on construction -- a crash with no
    // obvious cause and nothing in the interface to warn about it. Large tables
    // belong on the heap; the NNUE accumulator stack already is there for this
    // reason.
    static_assert(sizeof(SearchEngine) < 192 * 1024,
        "SearchEngine has grown too large to sit on a worker thread's stack; "
        "move the new storage to the heap");
}
