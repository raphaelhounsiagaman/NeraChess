#include "SearchEngine.h"

#include "ChessUtil.h"
#include "Evaluation.h"
#include "MoveOrdering.h"
#include "NnueEvaluator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <system_error>
#include <thread>
#include <utility>

namespace NeraChessSearch
{
    using namespace NeraChessEngine;

    namespace
    {
        constexpr std::array<int, 6> PieceValues = { 100, 320, 330, 500, 900, 0 };

        // Selective-search tuning. Every margin is in centipawns and every depth is in
        // plies. The values follow the usual published ranges; they were checked against
        // the baseline with self-play rather than copied from another engine's tuning.
        constexpr int ReverseFutilityMaxDepth = 7;
        constexpr int ReverseFutilityMargin = 130;
        constexpr int FutilityMaxDepth = 6;
        constexpr int FutilityMargin = 90;
        constexpr int FutilityBase = 70;
        constexpr int LateMovePruningMaxDepth = 8;
        constexpr int InternalIterativeReductionMinDepth = 4;
        // Only the quiet moves that plausibly caused a cutoff need a history malus.
        constexpr size_t MaxTrackedQuietMoves = 64;

        // Late move reductions, indexed by remaining depth and move number. The
        // logarithmic shape is what lets late quiet moves lose several plies at high
        // depth while the first few moves stay nearly full depth.
        constexpr int ReductionDepthLimit = 64;
        constexpr int ReductionMoveLimit = 64;

        std::array<std::array<int8_t, ReductionMoveLimit>, ReductionDepthLimit> BuildReductions()
        {
            std::array<std::array<int8_t, ReductionMoveLimit>, ReductionDepthLimit> table{};
            for (int depth = 1; depth < ReductionDepthLimit; ++depth)
            {
                for (int moveNumber = 1; moveNumber < ReductionMoveLimit; ++moveNumber)
                {
                    const double reduction = 0.85 +
                        std::log(static_cast<double>(depth)) *
                            std::log(static_cast<double>(moveNumber)) / 2.30;
                    table[depth][moveNumber] =
                        static_cast<int8_t>(std::clamp(static_cast<int>(reduction), 0, 31));
                }
            }
            return table;
        }

        const std::array<std::array<int8_t, ReductionMoveLimit>, ReductionDepthLimit> Reductions =
            BuildReductions();
    }

    SearchEngine::SearchEngine(size_t hashMegabytes)
        : m_TranspositionTable(std::make_shared<TranspositionTable>(hashMegabytes)),
          m_SharedState(std::make_shared<SharedSearchState>())
    {
    }

    SearchEngine::SearchEngine(size_t workerIndex,
        std::shared_ptr<TranspositionTable> transpositionTable,
        std::shared_ptr<SharedSearchState> sharedState)
        : m_TranspositionTable(std::move(transpositionTable)),
          m_SharedState(std::move(sharedState)),
          m_WorkerIndex(workerIndex)
    {
    }

    void SearchEngine::PrepareSearch(bool pondering)
    {
        m_SharedState->stopRequested = false;
        m_SharedState->searchStopped = false;
        m_SharedState->timeControlStartMilliseconds = pondering ? -1 : SteadyMilliseconds();
        m_TimeControlPrepared = true;
    }

    void SearchEngine::PonderHit()
    {
        int64_t pondering = -1;
        m_SharedState->timeControlStartMilliseconds.compare_exchange_strong(
            pondering, SteadyMilliseconds());
    }

    void SearchEngine::NewGame()
    {
        m_SharedState->stopRequested = false;
        m_SharedState->searchStopped = false;
        m_SharedState->nodes = 0;
        m_TimeControlPrepared = false;
        m_SharedState->timeControlStartMilliseconds = 0;
        m_TranspositionTable->Clear();
        ResetHeuristics();
        for (const std::unique_ptr<SearchEngine>& helper : m_HelperEngines)
            helper->ResetHeuristics();
    }

    void SearchEngine::ResizeHash(size_t megabytes)
    {
        m_TranspositionTable->Resize(megabytes);
    }

    void SearchEngine::SetThreadCount(size_t threadCount)
    {
        const size_t requestedThreadCount = std::clamp<size_t>(threadCount, 1, 256);
        std::vector<std::unique_ptr<SearchEngine>> helpers;
        helpers.reserve(requestedThreadCount - 1);
        for (size_t workerIndex = 1; workerIndex < requestedThreadCount; ++workerIndex)
        {
            helpers.push_back(std::unique_ptr<SearchEngine>(
                new SearchEngine(workerIndex, m_TranspositionTable, m_SharedState)));
        }
        m_HelperEngines = std::move(helpers);
        m_ThreadCount = requestedThreadCount;
    }

    void SearchEngine::ResetHeuristics()
    {
        m_KillerMoves = {};
        m_History = {};
        m_CounterMoves = {};
        m_PvTable = {};
        m_PvLength = {};
        m_StaticEval.fill(SCORE_NONE);
    }

    SearchResult SearchEngine::Search(const ChessBoard& position, const SearchLimits& limits)
    {
        m_StartTime = std::chrono::steady_clock::now();
        if (!m_TimeControlPrepared.exchange(false))
            m_SharedState->timeControlStartMilliseconds = SteadyMilliseconds();
        m_SharedState->searchStopped = false;
        m_SharedState->nodes = 0;
        m_TranspositionTable->NewSearch();

        std::vector<std::jthread> helperThreads;
        helperThreads.reserve(m_HelperEngines.size());
        std::vector<std::exception_ptr> helperErrors(m_HelperEngines.size());
        SearchResult result;
        try
        {
            for (size_t helperIndex = 0; helperIndex < m_HelperEngines.size(); ++helperIndex)
            {
                const std::unique_ptr<SearchEngine>& helper = m_HelperEngines[helperIndex];
                SearchLimits helperLimits = limits;
                helperLimits.iterationCallback = {};
                try
                {
                    helperThreads.emplace_back([worker = helper.get(), &position,
                        helperLimits = std::move(helperLimits), &helperErrors, helperIndex]() mutable
                    {
                        try
                        {
                            worker->SearchWorker(position, helperLimits);
                        }
                        catch (...)
                        {
                            helperErrors[helperIndex] = std::current_exception();
                            worker->m_SharedState->searchStopped = true;
                        }
                    });
                }
                catch (const std::system_error&)
                {
                    // Continue with the workers the operating system could create.
                    break;
                }
            }
            result = SearchWorker(position, limits);
        }
        catch (...)
        {
            m_SharedState->searchStopped = true;
            for (std::jthread& helperThread : helperThreads)
            {
                if (helperThread.joinable())
                    helperThread.join();
            }
            throw;
        }

        m_SharedState->searchStopped = true;
        for (std::jthread& helperThread : helperThreads)
            helperThread.join();
        for (const std::exception_ptr& helperError : helperErrors)
        {
            if (helperError)
                std::rethrow_exception(helperError);
        }

        result.nodes = m_SharedState->nodes.load(std::memory_order_relaxed);
        result.selectiveDepth = m_SelectiveDepth;
        for (size_t helperIndex = 0; helperIndex < helperThreads.size(); ++helperIndex)
        {
            result.selectiveDepth = std::max(result.selectiveDepth,
                m_HelperEngines[helperIndex]->m_SelectiveDepth);
        }
        result.hashFullPermill = m_TranspositionTable->HashFullPermill();
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_StartTime);
        return result;
    }

    SearchResult SearchEngine::SearchWorker(const ChessBoard& position,
        const SearchLimits& limits)
    {
        m_Limits = limits;
        m_Limits.maxDepth = std::clamp(m_Limits.maxDepth, 1, MAX_PLY - 1);
        if (m_WorkerIndex != 0)
            m_StartTime = std::chrono::steady_clock::now();
        m_Aborted = false;
        m_Nodes = 0;
        m_UnflushedNodes = 0;
        m_SelectiveDepth = 0;
        m_PvLength.fill(0);
        m_StaticEval.fill(SCORE_NONE);
        for (auto& side : m_History)
        {
            for (auto& from : side)
            {
                for (int32_t& score : from)
                    score /= 2;
            }
        }

        ChessBoard board = position;
        m_Network = &NeraChessNNUE::Evaluator::GetNetwork();
        m_Accumulators.Reset(*m_Network, board.GetBoardState());

        SearchResult result;
        Score rootTerminalScore;
        if (IsDrawOrTerminal(board, rootTerminalScore, 0))
        {
            result.score = rootTerminalScore;
            result.completed = true;
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_StartTime);
            FlushNodeCount();
            result.nodes = AggregateNodeCount();
            return result;
        }

        const MoveList<218> legalMoves = board.GetLegalMoves();
        for (const Move move : legalMoves)
        {
            if (IsRootMoveAllowed(move))
            {
                result.bestMove = move;
                break;
            }
        }
        if (result.bestMove == 0)
        {
            result.completed = true;
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_StartTime);
            FlushNodeCount();
            result.nodes = AggregateNodeCount();
            return result;
        }
        Score previousScore = SCORE_DRAW;

        for (int depth = 1; depth <= m_Limits.maxDepth; ++depth)
        {
            Score window = depth >= 4 ? 25 : SCORE_INF;
            Score alpha = window == SCORE_INF ? -SCORE_INF : previousScore - window;
            Score beta = window == SCORE_INF ? SCORE_INF : previousScore + window;
            RootResult iteration;

            while (true)
            {
                iteration = SearchRoot(board, depth, alpha, beta);
                if (m_Aborted)
                    break;
                if (iteration.score > alpha && iteration.score < beta)
                    break;

                window = std::min<Score>(SCORE_INF, window * 2);
                alpha = window == SCORE_INF
                    ? -SCORE_INF
                    : std::max(-SCORE_INF, previousScore - window);
                beta = window == SCORE_INF
                    ? SCORE_INF
                    : std::min(SCORE_INF, previousScore + window);
            }

            if (m_Aborted)
                break;

            result.bestMove = iteration.move;
            result.score = iteration.score;
            result.completedDepth = depth;
            result.completed = true;
            result.principalVariation.assign(m_PvTable[0].begin(),
                m_PvTable[0].begin() + m_PvLength[0]);
            previousScore = iteration.score;
            result.nodes = AggregateNodeCount();
            result.selectiveDepth = m_SelectiveDepth;
            result.hashFullPermill = m_TranspositionTable->HashFullPermill();
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_StartTime);
            if (m_Limits.iterationCallback)
                m_Limits.iterationCallback(result);

            if (std::abs(result.score) >= SCORE_MATE - MAX_PLY)
                break;
            if (m_Limits.softTime.count() > 0 &&
                TimeControlElapsed() >= m_Limits.softTime)
                break;
        }

        // Every MakeMove in the search must be matched by an accumulator push
        // and every UndoMove by a pop, or the accumulator stops describing the
        // board it is evaluated against. That is harmless while entries are
        // refreshed on demand and silently wrong the moment incremental
        // updates land, so catch the imbalance now rather than then.
        assert(m_Accumulators.Depth() == 0 &&
            "accumulator stack did not unwind; a make/unmake pair is missing its push/pop");

        FlushNodeCount();
        result.nodes = AggregateNodeCount();
        result.selectiveDepth = m_SelectiveDepth;
        result.hashFullPermill = m_TranspositionTable->HashFullPermill();
        result.aborted = m_Aborted;
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_StartTime);
        return result;
    }

    SearchEngine::RootResult SearchEngine::SearchRoot(ChessBoard& board,
        int depth, Score alpha, Score beta)
    {
        RootResult result;
        const Score originalAlpha = alpha;
        MoveList<218> moves;
        for (const Move move : board.GetLegalMoves())
        {
            if (IsRootMoveAllowed(move))
                moves.push(move);
        }
        const std::optional<TTEntry> ttEntry =
            m_TranspositionTable->Probe(board.GetZobristKey());
        const Move ttMove = ttEntry ? Move(ttEntry->move) : Move(0);
        SortMoves(board, moves, 0, ttMove);
        if (m_WorkerIndex > 0 && moves.size() > 2)
        {
            // Preserve the strongest candidate while making helpers explore the rest
            // of the root in different orders and feed those results into the shared TT.
            const size_t rotation = 1 +
                (m_WorkerIndex + static_cast<size_t>(depth)) % (moves.size() - 1);
            if (rotation > 1)
                std::rotate(moves.begin() + 1, moves.begin() + rotation, moves.end());
        }

        m_PvLength[0] = 0;
        // Seed the static-evaluation stack so the "improving" test is meaningful from
        // ply 2 onwards instead of silently disabled for the first two plies. The
        // accumulator was reset for this search before the root was entered, so the
        // NNUE evaluation is the right one to seed with (see EvaluateNode).
        m_StaticEval[0] = board.IsInCheck() ? SCORE_NONE : EvaluateNode(board);
        bool firstMove = true;
        for (const Move move : moves)
        {
            MakeSearchMove(board, move);
            Score score;
            if (firstMove)
            {
                score = -PrincipalVariationSearch(board, -beta, -alpha,
                    depth - 1, 1, true, true, move, false);
            }
            else
            {
                score = -PrincipalVariationSearch(board, -alpha - 1, -alpha,
                    depth - 1, 1, false, true, move, true);
                if (!m_Aborted && score > alpha && score < beta)
                    score = -PrincipalVariationSearch(board, -beta, -alpha,
                        depth - 1, 1, true, true, move, false);
            }
            UndoSearchMove(board, move);

            if (m_Aborted)
                return result;

            if (result.move == 0 || score > result.score)
            {
                result.move = move;
                result.score = score;
            }
            if (score > alpha)
            {
                alpha = score;
                UpdatePrincipalVariation(0, move);
            }
            if (alpha >= beta)
                break;
            firstMove = false;
        }

        TTBound bound = TTBound::Exact;
        if (result.score <= originalAlpha)
            bound = TTBound::Upper;
        else if (result.score >= beta)
            bound = TTBound::Lower;
        if (m_Limits.rootMoves.empty() && board.GetHalfMoveClock() < 90)
        {
            m_TranspositionTable->Store(board.GetZobristKey(), ScoreToTT(result.score, 0),
                depth, bound, result.move);
        }
        return result;
    }

    Score SearchEngine::PrincipalVariationSearch(ChessBoard& board,
        Score alpha, Score beta, int depth, int ply, bool pvNode, bool allowNull,
        Move previousMove, bool cutNode)
    {
        if (ShouldStop())
            return SCORE_DRAW;

        CountNode();
        m_SelectiveDepth = std::max(m_SelectiveDepth, ply);
        m_PvLength[ply] = ply;

        Score terminalScore;
        if (IsDrawOrTerminal(board, terminalScore, ply))
            return terminalScore;
        if (ply >= MAX_PLY - 1)
            return EvaluateNode(board);

        alpha = std::max(alpha, -SCORE_MATE + ply);
        beta = std::min(beta, SCORE_MATE - ply - 1);
        if (alpha >= beta)
            return alpha;
        if (depth <= 0)
            return QuiescenceSearch(board, alpha, beta, ply);

        const uint64_t key = board.GetZobristKey();
        const bool ttScoreUsable = board.GetHalfMoveClock() < 90;
        const std::optional<TTEntry> ttEntry = m_TranspositionTable->Probe(key);
        Move ttMove = 0;
        if (ttEntry)
        {
            ttMove = Move(ttEntry->move);
            const Score ttScore = ScoreFromTT(ttEntry->score, ply);
            if (ttScoreUsable && !pvNode && ttEntry->depth >= depth)
            {
                if (ttEntry->GetBound() == TTBound::Exact)
                    return ttScore;
                if (ttEntry->GetBound() == TTBound::Lower && ttScore >= beta)
                    return ttScore;
                if (ttEntry->GetBound() == TTBound::Upper && ttScore <= alpha)
                    return ttScore;
            }
        }

        const bool inCheck = board.IsInCheck();
        const bool mateBounds = beta <= -SCORE_MATE + MAX_PLY ||
            beta >= SCORE_MATE - MAX_PLY;

        // The static evaluation of every ply is kept so a node can tell whether its own
        // side is improving relative to two plies ago. Pruning margins and reductions
        // are all conditioned on it: a position that is getting worse deserves a wider,
        // more careful search than one that is getting better.
        //
        // EvaluateNode, not Evaluate: the former reads this worker's NNUE accumulator
        // for the current ply, the latter is the hand-crafted evaluation. Using the
        // wrong one here would quietly make the branch part-NNUE and invalidate any
        // NNUE-vs-classical comparison.
        const Score staticEval = inCheck ? SCORE_NONE : EvaluateNode(board);
        m_StaticEval[ply] = staticEval;
        const bool improving = !inCheck && ply >= 2 &&
            m_StaticEval[ply - 2] != SCORE_NONE && staticEval > m_StaticEval[ply - 2];
        const bool canPrune = !pvNode && !inCheck && !mateBounds;

        // Reverse futility: the side to move is so far ahead that even conceding
        // ReverseFutilityMargin per remaining ply keeps it above beta.
        if (canPrune && depth <= ReverseFutilityMaxDepth &&
            staticEval - ReverseFutilityMargin * (depth - improving) >= beta)
        {
            return staticEval;
        }

        // A null move leaves every piece where it is, so the accumulator stays valid
        // and needs no push. Only the side to move changes, and the output layer
        // reads that from the board state.
        if (allowNull && canPrune && depth >= 3 && HasNonPawnMaterial(board) &&
            staticEval >= beta && board.MakeNullMove())
        {
            const int reduction = 3 + depth / 4 +
                std::min(3, (staticEval - beta) / 200);
            const Score nullScore = -PrincipalVariationSearch(board, -beta, -beta + 1,
                depth - 1 - reduction, ply + 1, false, false, 0, !cutNode);
            board.UndoNullMove();

            if (m_Aborted)
                return SCORE_DRAW;
            if (nullScore >= beta)
            {
                if (ttScoreUsable)
                {
                    m_TranspositionTable->Store(key, ScoreToTT(beta, ply), depth,
                        TTBound::Lower, ttMove);
                }
                return beta;
            }
        }

        // Internal iterative reduction: a node deep enough to matter but with no stored
        // move will order badly, so it is cheaper to search it a ply shallower and let
        // the resulting entry order the re-search.
        if (depth >= InternalIterativeReductionMinDepth && ttMove == 0 && (pvNode || cutNode))
            --depth;

        const Score originalAlpha = alpha;
        MoveList<218> moves = board.GetLegalMoves();
        SortMoves(board, moves, ply, ttMove, previousMove);

        Score bestScore = -SCORE_INF;
        Move bestMove = 0;
        std::array<Move, MaxTrackedQuietMoves> quietMovesSearched;
        size_t quietMoveCount = 0;
        const Move counterMove = GetCounterMove(previousMove);
        const uint8_t side = board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove) ? 0 : 1;
        const int lateMoveCountLimit = LateMoveCountLimit(depth, improving);
        bool firstMove = true;
        int moveIndex = 0;
        for (const Move move : moves)
        {
            const bool quiet = IsQuiet(move);
            const bool killer = quiet &&
                (move == m_KillerMoves[ply][0] || move == m_KillerMoves[ply][1]);
            const bool counter = quiet && move == counterMove;

            // Everything below decides against a move without making it. The board is
            // only touched once a move survives, which is what makes a wider pruning
            // layer cheaper than the narrow one it replaces. Checking moves are always
            // exempt: a quiet check is exactly the kind of move that decides a forced
            // line, and it is also the kind that orders badly enough to be pruned.
            bool givesCheck = false;
            if (canPrune && quiet && bestScore > -SCORE_INF)
            {
                const bool prunableByCount =
                    depth <= LateMovePruningMaxDepth && moveIndex >= lateMoveCountLimit;
                const bool prunableByMargin = depth <= FutilityMaxDepth && !killer && !counter &&
                    staticEval + FutilityMargin * depth + FutilityBase <= alpha &&
                    IsFutilityPrunable(move);
                if (prunableByCount || prunableByMargin)
                {
                    givesCheck = board.GivesCheck(move);
                    if (!givesCheck)
                    {
                        // Skipping rather than leaving the loop: killers and counter
                        // moves sort ahead of losing captures, so breaking out here
                        // would also discard captures that still have to be searched.
                        ++moveIndex;
                        continue;
                    }
                }
            }

            // MakeSearchMove, not board.MakeMove: this branch keeps a per-ply NNUE
            // accumulator that has to be pushed with the move and popped with it.
            MakeSearchMove(board, move);
            givesCheck = board.IsInCheck();
            Score score;
            if (firstMove)
            {
                score = -PrincipalVariationSearch(board, -beta, -alpha,
                    depth - 1, ply + 1, pvNode, true, move, pvNode ? false : !cutNode);
            }
            else
            {
                const int reduction = quiet && !inCheck && !givesCheck
                    ? LateMoveReduction(depth, moveIndex, pvNode, cutNode, improving,
                        killer || counter,
                        m_History[side][move.GetStartSquare()][move.GetTargetSquare()])
                    : 0;

                score = -PrincipalVariationSearch(board, -alpha - 1, -alpha,
                    depth - 1 - reduction, ply + 1, false, true, move, true);
                if (!m_Aborted && reduction > 0 && score > alpha)
                {
                    score = -PrincipalVariationSearch(board, -alpha - 1, -alpha,
                        depth - 1, ply + 1, false, true, move, !cutNode);
                }
                if (!m_Aborted && pvNode && score > alpha && score < beta)
                    score = -PrincipalVariationSearch(board, -beta, -alpha,
                        depth - 1, ply + 1, true, true, move, false);
            }
            UndoSearchMove(board, move);

            if (m_Aborted)
                return SCORE_DRAW;

            if (quiet && quietMoveCount < quietMovesSearched.size())
                quietMovesSearched[quietMoveCount++] = move;

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }
            if (score > alpha)
            {
                alpha = score;
                UpdatePrincipalVariation(ply, move);
            }
            if (alpha >= beta)
            {
                if (IsQuiet(move))
                {
                    if (m_KillerMoves[ply][0] != move)
                    {
                        m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
                        m_KillerMoves[ply][0] = move;
                    }
                    const int bonus = std::min(16'384, 32 * depth * depth);
                    UpdateHistoryScore(
                        m_History[side][move.GetStartSquare()][move.GetTargetSquare()], bonus);
                    for (size_t index = 0; index < quietMoveCount; ++index)
                    {
                        const Move failed = quietMovesSearched[index];
                        if (failed == move)
                            continue;
                        UpdateHistoryScore(
                            m_History[side][failed.GetStartSquare()][failed.GetTargetSquare()],
                            -bonus / 2);
                    }
                    if (previousMove != 0)
                    {
                        m_CounterMoves[previousMove.GetMovePiece()]
                            [previousMove.GetTargetSquare()] = move;
                    }
                }
                break;
            }
            firstMove = false;
            ++moveIndex;
        }

        TTBound bound = TTBound::Exact;
        if (bestScore <= originalAlpha)
            bound = TTBound::Upper;
        else if (bestScore >= beta)
            bound = TTBound::Lower;
        if (ttScoreUsable)
            m_TranspositionTable->Store(key, ScoreToTT(bestScore, ply), depth, bound, bestMove);
        return bestScore;
    }

    Score SearchEngine::QuiescenceSearch(ChessBoard& board, Score alpha, Score beta, int ply)
    {
        if (ShouldStop())
            return SCORE_DRAW;

        CountNode();
        m_SelectiveDepth = std::max(m_SelectiveDepth, ply);
        m_PvLength[ply] = ply;

        Score terminalScore;
        if (IsDrawOrTerminal(board, terminalScore, ply))
            return terminalScore;
        if (ply >= MAX_PLY - 1)
            return EvaluateNode(board);

        alpha = std::max(alpha, -SCORE_MATE + ply);
        beta = std::min(beta, SCORE_MATE - ply - 1);
        if (alpha >= beta)
            return alpha;

        const uint64_t key = board.GetZobristKey();
        const bool ttScoreUsable = board.GetHalfMoveClock() < 90;
        const std::optional<TTEntry> ttEntry = m_TranspositionTable->Probe(key);
        Move ttMove = 0;
        if (ttEntry)
        {
            ttMove = Move(ttEntry->move);
            const Score ttScore = ScoreFromTT(ttEntry->score, ply);
            if (ttScoreUsable)
            {
                if (ttEntry->GetBound() == TTBound::Exact)
                    return ttScore;
                if (ttEntry->GetBound() == TTBound::Lower && ttScore >= beta)
                    return ttScore;
                if (ttEntry->GetBound() == TTBound::Upper && ttScore <= alpha)
                    return ttScore;
            }
        }

        const Score originalAlpha = alpha;
        const bool inCheck = board.IsInCheck();
        Score bestScore = -SCORE_INF;
        Move bestMove = 0;
        Score standPat = -SCORE_INF;
        if (!inCheck)
        {
            standPat = EvaluateNode(board);
            bestScore = standPat;
            if (bestScore >= beta)
            {
                if (ttScoreUsable)
                {
                    m_TranspositionTable->Store(key, ScoreToTT(bestScore, ply), 0,
                        TTBound::Lower, ttMove);
                }
                return bestScore;
            }
            alpha = std::max(alpha, bestScore);
        }

        MoveList<218> candidates;
        for (const Move move : board.GetLegalMoves())
        {
            if (inCheck || !IsQuiet(move))
                candidates.push(move);
        }
        SortMoves(board, candidates, ply, ttMove);

        for (const Move move : candidates)
        {
            bool deltaPrunable = false;
            bool seePrunable = false;
            if (!inCheck && !(move.GetMoveFlags() & MoveFlags::IS_PROMOTION))
            {
                const Piece victim = (move.GetMoveFlags() & MoveFlags::IS_EN_PASSANT)
                    ? Piece(move.GetMovePiece().IsWhite()
                        ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN)
                    : board.GetPiece(move.GetTargetSquare());
                deltaPrunable = standPat + PieceValue(victim) + 200 < alpha;
                seePrunable = MoveOrdering::StaticExchangeEvaluation(board, move) < -50;
            }

            MakeSearchMove(board, move);
            if (!inCheck && !board.IsInCheck() && (deltaPrunable || seePrunable))
            {
                UndoSearchMove(board, move);
                continue;
            }
            const Score score = -QuiescenceSearch(board, -beta, -alpha, ply + 1);
            UndoSearchMove(board, move);

            if (m_Aborted)
                return SCORE_DRAW;
            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }
            if (score > alpha)
            {
                alpha = score;
                UpdatePrincipalVariation(ply, move);
            }
            if (alpha >= beta)
            {
                if (ttScoreUsable)
                {
                    m_TranspositionTable->Store(key, ScoreToTT(bestScore, ply), 0,
                        TTBound::Lower, bestMove);
                }
                return bestScore;
            }
        }

        const TTBound bound = bestScore <= originalAlpha ? TTBound::Upper : TTBound::Exact;
        if (ttScoreUsable)
            m_TranspositionTable->Store(key, ScoreToTT(bestScore, ply), 0, bound, bestMove);
        return bestScore;
    }

    void SearchEngine::SortMoves(const ChessBoard& board, MoveList<218>& moves,
        int ply, Move ttMove, Move previousMove)
    {
        std::array<int32_t, 218> scores{};
        const Move counterMove = GetCounterMove(previousMove);
        const uint8_t side = board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove) ? 0 : 1;
        for (size_t i = 0; i < moves.size(); ++i)
        {
            const Move move = moves[i];
            int32_t score = 0;
            if (move == ttMove)
            {
                scores[i] = 20'000'000;
                continue;
            }

            if (move.GetMoveFlags() & MoveFlags::IS_CAPTURE)
            {
                const Piece victim = (move.GetMoveFlags() & MoveFlags::IS_EN_PASSANT)
                    ? Piece(move.GetMovePiece().IsWhite() ? PieceType::BLACK_PAWN : PieceType::WHITE_PAWN)
                    : board.GetPiece(move.GetTargetSquare());
                const int see = MoveOrdering::StaticExchangeEvaluation(board, move);
                score += (see >= 0 ? 10'000'000 : 5'000'000) +
                    PieceValue(victim) * 16 - PieceValue(move.GetMovePiece()) + see;
            }
            if (move.GetMoveFlags() & MoveFlags::IS_PROMOTION)
                score += 9'000'000 + PieceValue(move.GetPromoPiece());

            if (IsQuiet(move) && ply < MAX_PLY)
            {
                if (move == m_KillerMoves[ply][0])
                    score += 8'000'000;
                else if (move == m_KillerMoves[ply][1])
                    score += 7'000'000;
                else if (move == counterMove)
                    score += 6'000'000;
                score += m_History[side][move.GetStartSquare()][move.GetTargetSquare()];
            }
            scores[i] = score;
        }

        for (size_t i = 1; i < moves.size(); ++i)
        {
            const Move move = moves[i];
            const int32_t score = scores[i];
            size_t j = i;
            while (j > 0 && scores[j - 1] < score)
            {
                scores[j] = scores[j - 1];
                moves[j] = moves[j - 1];
                --j;
            }
            scores[j] = score;
            moves[j] = move;
        }
    }

    void SearchEngine::UpdatePrincipalVariation(int ply, Move move)
    {
        m_PvTable[ply][ply] = move;
        const int childLength = ply + 1 < MAX_PLY ? m_PvLength[ply + 1] : ply + 1;
        for (int index = ply + 1; index < childLength; ++index)
            m_PvTable[ply][index] = m_PvTable[ply + 1][index];
        m_PvLength[ply] = childLength;
    }

    void SearchEngine::CountNode()
    {
        // Batching avoids an atomic increment at every node. The global node limit
        // can overshoot by at most one partial batch per worker.
        constexpr uint64_t NodeFlushInterval = 256;
        ++m_Nodes;
        ++m_UnflushedNodes;
        if (m_UnflushedNodes >= NodeFlushInterval)
            FlushNodeCount();
    }

    void SearchEngine::FlushNodeCount()
    {
        if (m_UnflushedNodes == 0)
            return;
        m_SharedState->nodes.fetch_add(m_UnflushedNodes, std::memory_order_relaxed);
        m_UnflushedNodes = 0;
    }

    uint64_t SearchEngine::AggregateNodeCount() const
    {
        return m_SharedState->nodes.load(std::memory_order_relaxed) + m_UnflushedNodes;
    }

    bool SearchEngine::ShouldStop()
    {
        if (m_Aborted)
            return true;
        if (m_SharedState->stopRequested.load(std::memory_order_relaxed) ||
            m_SharedState->searchStopped.load(std::memory_order_relaxed))
        {
            m_Aborted = true;
            return true;
        }
        if (m_Limits.maxNodes > 0 && AggregateNodeCount() >= m_Limits.maxNodes)
        {
            m_SharedState->searchStopped = true;
            m_Aborted = true;
            return true;
        }
        if (m_Limits.hardTime.count() > 0 && (m_Nodes & 1023) == 0 &&
            TimeControlElapsed() >= m_Limits.hardTime)
        {
            m_SharedState->searchStopped = true;
            m_Aborted = true;
            return true;
        }
        return false;
    }

    std::chrono::milliseconds SearchEngine::TimeControlElapsed() const
    {
        const int64_t start =
            m_SharedState->timeControlStartMilliseconds.load(std::memory_order_relaxed);
        if (start < 0)
            return std::chrono::milliseconds{ 0 };
        return std::chrono::milliseconds{ std::max<int64_t>(0, SteadyMilliseconds() - start) };
    }

    int64_t SearchEngine::SteadyMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool SearchEngine::IsDrawOrTerminal(const ChessBoard& board,
        Score& terminalScore, int ply) const
    {
        const uint16_t flags = board.GetGameOver(true);
        if (flags == GameOverFlags::IS_GAME_CONTINUE)
            return false;
        terminalScore = (flags & GameOverFlags::IS_CHECKMATE)
            ? -SCORE_MATE + ply
            : SCORE_DRAW;
        return true;
    }

    Score SearchEngine::ScoreToTT(Score score, int ply)
    {
        if (score >= SCORE_MATE - MAX_PLY)
            return score + ply;
        if (score <= -SCORE_MATE + MAX_PLY)
            return score - ply;
        return score;
    }

    Score SearchEngine::ScoreFromTT(Score score, int ply)
    {
        if (score >= SCORE_MATE - MAX_PLY)
            return score - ply;
        if (score <= -SCORE_MATE + MAX_PLY)
            return score + ply;
        return score;
    }

    int SearchEngine::PieceValue(Piece piece)
    {
        if (piece == PieceType::NO_PIECE)
            return 0;
        return PieceValues[static_cast<uint8_t>(piece) % 6];
    }

    bool SearchEngine::IsQuiet(Move move)
    {
        return !(move.GetMoveFlags() & (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION));
    }

    bool SearchEngine::IsFutilityPrunable(Move move)
    {
        if (!IsQuiet(move) || (move.GetMoveFlags() & MoveFlags::IS_CASTLES))
            return false;
        const Piece piece = move.GetMovePiece();
        if (piece == PieceType::WHITE_PAWN && move.GetTargetSquare().GetRank() >= 6)
            return false;
        if (piece == PieceType::BLACK_PAWN && move.GetTargetSquare().GetRank() <= 1)
            return false;
        return true;
    }

    int SearchEngine::LateMoveReduction(int depth, int moveIndex, bool pvNode, bool cutNode,
        bool improving, bool orderedQuiet, int32_t historyScore)
    {
        if (depth < 3 || moveIndex < 3)
            return 0;

        int reduction = Reductions[std::min(depth, ReductionDepthLimit - 1)]
                                  [std::min(moveIndex, ReductionMoveLimit - 1)];
        if (pvNode)
            --reduction;
        if (improving)
            --reduction;
        if (orderedQuiet)
            --reduction;
        if (cutNode)
            ++reduction;
        // History is clamped to +/-16384, so this shifts the reduction by at most four
        // plies either way based on how the move has actually performed.
        reduction -= historyScore / 4096;
        return std::clamp(reduction, 0, depth - 2);
    }

    int SearchEngine::LateMoveCountLimit(int depth, bool improving)
    {
        const int limited = std::min(depth, LateMovePruningMaxDepth);
        return (3 + limited * limited) / (improving ? 1 : 2);
    }

    bool SearchEngine::HasNonPawnMaterial(const ChessBoard& board)
    {
        const BoardState& state = board.GetBoardState();
        const uint8_t offset = state.HasFlag(BoardStateFlags::WhiteToMove) ? 0 : 6;
        return state.pieceBitboards[offset + PieceType::WHITE_KNIGHT] |
            state.pieceBitboards[offset + PieceType::WHITE_BISHOP] |
            state.pieceBitboards[offset + PieceType::WHITE_ROOK] |
            state.pieceBitboards[offset + PieceType::WHITE_QUEEN];
    }

    bool SearchEngine::IsRootMoveAllowed(Move move) const
    {
        return m_Limits.rootMoves.empty() ||
            std::find(m_Limits.rootMoves.begin(), m_Limits.rootMoves.end(), move) !=
                m_Limits.rootMoves.end();
    }

    Move SearchEngine::GetCounterMove(Move previousMove) const
    {
        if (previousMove == 0 || previousMove.GetMovePiece() >= 12)
            return 0;
        return m_CounterMoves[previousMove.GetMovePiece()][previousMove.GetTargetSquare()];
    }

    void SearchEngine::UpdateHistoryScore(int32_t& score, int bonus)
    {
        bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
        score += bonus - score * std::abs(bonus) / MAX_HISTORY;
    }

    Score SearchEngine::Evaluate(const ChessBoard& board)
    {
        return Evaluation::Evaluate(board);
    }

    Score SearchEngine::EvaluateNode(const ChessBoard& board)
    {
        // Reads this worker's accumulator for the current ply, which
        // MakeSearchMove and UndoSearchMove keep aligned with the board.
        return Evaluation::Evaluate(board.GetBoardState(), m_Accumulators.Current());
    }

    void SearchEngine::MakeSearchMove(ChessBoard& board, Move move)
    {
        if (m_Network == nullptr || !m_Network->IsLoaded())
        {
            // Nothing to update, and reading the captured piece off the board
            // would be pure overhead.
            board.MakeMove(move);
            m_Accumulators.PushStale();
            return;
        }

        // The captured piece has to be read before the move is made; the move
        // encoding does not carry it.
        const NeraChessNNUE::DirtyPieces dirty = NeraChessNNUE::DescribeMove(board, move);
        board.MakeMove(move);
        m_Accumulators.Push(*m_Network, board.GetBoardState(), dirty);
    }

    void SearchEngine::UndoSearchMove(ChessBoard& board, Move move)
    {
        board.UndoMove(move);
        m_Accumulators.Pop();
    }
}
