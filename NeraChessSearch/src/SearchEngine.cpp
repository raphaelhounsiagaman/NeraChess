#include "SearchEngine.h"

#include "ChessUtil.h"
#include "Evaluation.h"
#include "MoveOrdering.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace NeraChessSearch
{
    using namespace NeraChessEngine;

    namespace
    {
        constexpr std::array<int, 6> PieceValues = { 100, 320, 330, 500, 900, 0 };
    }

    SearchEngine::SearchEngine(size_t hashMegabytes)
        : m_TranspositionTable(hashMegabytes)
    {
    }

    void SearchEngine::NewGame()
    {
        m_StopRequested = false;
        m_TranspositionTable.Clear();
        m_KillerMoves = {};
        m_History = {};
    }

    SearchResult SearchEngine::Search(const ChessBoard& position, const SearchLimits& limits)
    {
        m_Limits = limits;
        m_Limits.maxDepth = std::clamp(m_Limits.maxDepth, 1, MAX_PLY - 1);
        m_StartTime = std::chrono::steady_clock::now();
        m_Aborted = false;
        m_Nodes = 0;
        m_SelectiveDepth = 0;
        m_PvLength.fill(0);
        m_TranspositionTable.NewSearch();

        ChessBoard board = position;
        SearchResult result;
        Score rootTerminalScore;
        if (IsDrawOrTerminal(board, rootTerminalScore, 0))
        {
            result.score = rootTerminalScore;
            result.completed = true;
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_StartTime);
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
            result.nodes = m_Nodes;
            result.selectiveDepth = m_SelectiveDepth;
            result.hashFullPermill = m_TranspositionTable.HashFullPermill();
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_StartTime);
            if (m_Limits.iterationCallback)
                m_Limits.iterationCallback(result);

            if (std::abs(result.score) >= SCORE_MATE - MAX_PLY)
                break;
            if (m_Limits.softTime.count() > 0 &&
                std::chrono::steady_clock::now() - m_StartTime >= m_Limits.softTime)
                break;
        }

        result.nodes = m_Nodes;
        result.selectiveDepth = m_SelectiveDepth;
        result.hashFullPermill = m_TranspositionTable.HashFullPermill();
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
        const TTEntry* ttEntry = m_TranspositionTable.Probe(board.GetZobristKey());
        const Move ttMove = ttEntry ? Move(ttEntry->move) : Move(0);
        SortMoves(board, moves, 0, ttMove);

        m_PvLength[0] = 0;
        bool firstMove = true;
        for (const Move move : moves)
        {
            board.MakeMove(move);
            Score score;
            if (firstMove)
            {
                score = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, 1, true, true);
            }
            else
            {
                score = -PrincipalVariationSearch(board, -alpha - 1, -alpha, depth - 1, 1, false, true);
                if (!m_Aborted && score > alpha && score < beta)
                    score = -PrincipalVariationSearch(board, -beta, -alpha, depth - 1, 1, true, true);
            }
            board.UndoMove(move);

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
        if (m_Limits.rootMoves.empty())
        {
            m_TranspositionTable.Store(board.GetZobristKey(), ScoreToTT(result.score, 0),
                depth, bound, result.move);
        }
        return result;
    }

    Score SearchEngine::PrincipalVariationSearch(ChessBoard& board,
        Score alpha, Score beta, int depth, int ply, bool pvNode, bool allowNull)
    {
        if (ShouldStop())
            return SCORE_DRAW;

        ++m_Nodes;
        m_SelectiveDepth = std::max(m_SelectiveDepth, ply);
        m_PvLength[ply] = ply;

        Score terminalScore;
        if (IsDrawOrTerminal(board, terminalScore, ply))
            return terminalScore;
        if (ply >= MAX_PLY - 1)
            return Evaluate(board);
        if (depth <= 0)
            return QuiescenceSearch(board, alpha, beta, ply);

        const uint64_t key = board.GetZobristKey();
        const TTEntry* ttEntry = m_TranspositionTable.Probe(key);
        Move ttMove = 0;
        if (ttEntry)
        {
            ttMove = Move(ttEntry->move);
            const Score ttScore = ScoreFromTT(ttEntry->score, ply);
            if (!pvNode && ttEntry->depth >= depth)
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
        const bool canReverseFutility = !pvNode && !inCheck && !mateBounds && depth <= 2;
        const bool canFutilityPrune = !pvNode && !inCheck && !mateBounds && depth <= 2;
        const bool canNullPrune = allowNull && !pvNode && !inCheck && !mateBounds &&
            depth >= 3 && HasNonPawnMaterial(board);
        Score staticEval = SCORE_DRAW;
        if (canReverseFutility || canFutilityPrune || canNullPrune)
            staticEval = Evaluate(board);

        if (canReverseFutility && staticEval - 120 * depth >= beta)
            return staticEval;

        if (canNullPrune)
        {
            if (staticEval >= beta && board.MakeNullMove())
            {
                const int reduction = 2 + depth / 4;
                const Score nullScore = -PrincipalVariationSearch(board, -beta, -beta + 1,
                    depth - 1 - reduction, ply + 1, false, false);
                board.UndoNullMove();

                if (m_Aborted)
                    return SCORE_DRAW;
                if (nullScore >= beta)
                {
                    m_TranspositionTable.Store(key, ScoreToTT(beta, ply), depth,
                        TTBound::Lower, ttMove);
                    return beta;
                }
            }
        }

        const Score originalAlpha = alpha;
        MoveList<218> moves = board.GetLegalMoves();
        SortMoves(board, moves, ply, ttMove);

        Score bestScore = -SCORE_INF;
        Move bestMove = 0;
        bool firstMove = true;
        int moveIndex = 0;
        for (const Move move : moves)
        {
            const bool quiet = IsQuiet(move);
            const bool killer = quiet &&
                (move == m_KillerMoves[ply][0] || move == m_KillerMoves[ply][1]);
            board.MakeMove(move);
            const bool givesCheck = board.IsInCheck();
            if (canFutilityPrune && staticEval + 120 * depth <= alpha &&
                bestMove != 0 && !killer && !givesCheck && IsFutilityPrunable(move))
            {
                board.UndoMove(move);
                ++moveIndex;
                continue;
            }
            Score score;
            if (firstMove)
            {
                score = -PrincipalVariationSearch(board, -beta, -alpha,
                    depth - 1, ply + 1, pvNode, true);
            }
            else
            {
                int reduction = 0;
                if (depth >= 3 && moveIndex >= 3 && quiet && !killer &&
                    !inCheck && !givesCheck)
                {
                    reduction = LateMoveReduction(depth, moveIndex, pvNode);
                }

                score = -PrincipalVariationSearch(board, -alpha - 1, -alpha,
                    depth - 1 - reduction, ply + 1, false, true);
                if (!m_Aborted && reduction > 0 && score > alpha)
                {
                    score = -PrincipalVariationSearch(board, -alpha - 1, -alpha,
                        depth - 1, ply + 1, false, true);
                }
                if (!m_Aborted && pvNode && score > alpha && score < beta)
                    score = -PrincipalVariationSearch(board, -beta, -alpha,
                        depth - 1, ply + 1, true, true);
            }
            board.UndoMove(move);

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
                if (IsQuiet(move))
                {
                    if (m_KillerMoves[ply][0] != move)
                    {
                        m_KillerMoves[ply][1] = m_KillerMoves[ply][0];
                        m_KillerMoves[ply][0] = move;
                    }
                    int32_t& history = m_History[move.GetStartSquare()][move.GetTargetSquare()];
                    history = std::min<int32_t>(1'000'000, history + depth * depth);
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
        m_TranspositionTable.Store(key, ScoreToTT(bestScore, ply), depth, bound, bestMove);
        return bestScore;
    }

    Score SearchEngine::QuiescenceSearch(ChessBoard& board, Score alpha, Score beta, int ply)
    {
        if (ShouldStop())
            return SCORE_DRAW;

        ++m_Nodes;
        m_SelectiveDepth = std::max(m_SelectiveDepth, ply);
        m_PvLength[ply] = ply;

        Score terminalScore;
        if (IsDrawOrTerminal(board, terminalScore, ply))
            return terminalScore;
        if (ply >= MAX_PLY - 1)
            return Evaluate(board);

        const uint64_t key = board.GetZobristKey();
        const TTEntry* ttEntry = m_TranspositionTable.Probe(key);
        Move ttMove = 0;
        if (ttEntry)
        {
            ttMove = Move(ttEntry->move);
            const Score ttScore = ScoreFromTT(ttEntry->score, ply);
            if (ttEntry->GetBound() == TTBound::Exact)
                return ttScore;
            if (ttEntry->GetBound() == TTBound::Lower && ttScore >= beta)
                return ttScore;
            if (ttEntry->GetBound() == TTBound::Upper && ttScore <= alpha)
                return ttScore;
        }

        const Score originalAlpha = alpha;
        const bool inCheck = board.IsInCheck();
        Score bestScore = -SCORE_INF;
        Move bestMove = 0;
        Score standPat = -SCORE_INF;
        if (!inCheck)
        {
            standPat = Evaluate(board);
            bestScore = standPat;
            if (bestScore >= beta)
            {
                m_TranspositionTable.Store(key, ScoreToTT(bestScore, ply), 0,
                    TTBound::Lower, ttMove);
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

            board.MakeMove(move);
            if (!inCheck && !board.IsInCheck() && (deltaPrunable || seePrunable))
            {
                board.UndoMove(move);
                continue;
            }
            const Score score = -QuiescenceSearch(board, -beta, -alpha, ply + 1);
            board.UndoMove(move);

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
                m_TranspositionTable.Store(key, ScoreToTT(bestScore, ply), 0,
                    TTBound::Lower, bestMove);
                return bestScore;
            }
        }

        const TTBound bound = bestScore <= originalAlpha ? TTBound::Upper : TTBound::Exact;
        m_TranspositionTable.Store(key, ScoreToTT(bestScore, ply), 0, bound, bestMove);
        return bestScore;
    }

    void SearchEngine::SortMoves(const ChessBoard& board, MoveList<218>& moves,
        int ply, Move ttMove)
    {
        std::array<int32_t, 218> scores{};
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
                score += m_History[move.GetStartSquare()][move.GetTargetSquare()];
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

    bool SearchEngine::ShouldStop()
    {
        if (m_Aborted)
            return true;
        if (m_StopRequested)
        {
            m_Aborted = true;
            return true;
        }
        if (m_Limits.maxNodes > 0 && m_Nodes >= m_Limits.maxNodes)
        {
            m_Aborted = true;
            return true;
        }
        if (m_Limits.hardTime.count() > 0 && (m_Nodes & 1023) == 0 &&
            std::chrono::steady_clock::now() - m_StartTime >= m_Limits.hardTime)
        {
            m_Aborted = true;
            return true;
        }
        return false;
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

    int SearchEngine::LateMoveReduction(int depth, int moveIndex, bool pvNode)
    {
        int reduction = 1;
        if (depth >= 6)
            ++reduction;
        if (moveIndex >= 8)
            ++reduction;
        if (pvNode)
            --reduction;
        return std::clamp(reduction, 0, depth - 2);
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

    Score SearchEngine::Evaluate(const ChessBoard& board)
    {
        return Evaluation::Evaluate(board);
    }
}
