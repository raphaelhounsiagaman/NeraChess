#include "SelfPlay.h"

#include "ChessBoard.h"
#include "ChessUtil.h"
#include "Evaluation.h"
#include "NnueEvaluator.h"
#include "SearchEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace NeraChessSelfPlay
{
    using namespace NeraChessEngine;
    using NeraChessSearch::SCORE_MATE;
    using NeraChessSearch::MAX_PLY;

    namespace
    {
        // The one piece of chess knowledge this pipeline injects. Same values
        // the move ordering already uses for static exchange evaluation.
        constexpr std::array<int, 6> MaterialValues = { 100, 320, 330, 500, 900, 0 };

        // A game result in [0, 1] from White's point of view.
        constexpr double WhiteWin = 1.0;
        constexpr double BlackWin = 0.0;
        constexpr double Drawn = 0.5;

        int MaterialScore(const BoardState& state)
        {
            int score = 0;
            for (uint8_t piece = 0; piece < 12; ++piece)
            {
                const int count = BitUtil::PopCnt(state.pieceBitboards[piece]);
                const int sign = piece < 6 ? 1 : -1;
                score += sign * MaterialValues[piece % 6] * count;
            }
            return score;
        }

        bool IsMateScore(int score)
        {
            return std::abs(score) >= SCORE_MATE - MAX_PLY;
        }

        // Maps a centipawn score onto the win probability the training loss
        // compares against, so material-mode samples carry a result consistent
        // with their score whatever lambda the trainer uses.
        double ScoreToWinProbability(int score)
        {
            return 1.0 / (1.0 + std::exp(-static_cast<double>(score) / 400.0));
        }

        // A 64-bit LCG. Self-contained so a run reproduces from its seed alone,
        // independent of the standard library's generator implementation.
        class Random
        {
        public:
            explicit Random(uint64_t seed) : m_State(seed * 2 + 1) { Next(); }

            uint64_t Next()
            {
                m_State = m_State * 6'364'136'223'846'793'005ull + 1'442'695'040'888'963'407ull;
                return m_State >> 17;
            }

            size_t Below(size_t bound) { return bound == 0 ? 0 : Next() % bound; }

        private:
            uint64_t m_State;
        };

        struct Sample
        {
            std::string fen;
            int score = 0;
            double result = Drawn;
            // Recorded rather than re-derived from the FEN, so attaching the
            // game result later cannot depend on string parsing.
            bool whiteToMove = true;
        };

        // Serializes writes and stops every worker once the target is reached.
        //
        // A run that does not finish -- interrupted from the terminal, killed
        // by the scheduler, out of disk -- must not leave a half-written file
        // under the output name, because the next run would find it, assume it
        // is complete, and train on a truncated dataset. So the samples go to a
        // sibling ".partial" file that is renamed into place only once every
        // worker has stopped and the stream has flushed cleanly.
        //
        // Append mode cannot work that way, since it is adding to a file that
        // already matters. There an interrupted run can leave a torn final
        // line, which the trainer's reader tolerates.
        class SampleWriter
        {
        public:
            SampleWriter(const std::filesystem::path& path, bool append, uint64_t target)
                : m_FinalPath(path), m_Target(target)
            {
                std::error_code error;
                if (path.has_parent_path())
                    std::filesystem::create_directories(path.parent_path(), error);

                m_WritePath = append ? path : std::filesystem::path(path.string() + ".partial");
                m_Stream.open(m_WritePath, append
                    ? (std::ios::out | std::ios::app)
                    : (std::ios::out | std::ios::trunc));
            }

            bool IsOpen() const { return m_Stream.is_open(); }

            const std::filesystem::path& WritePath() const { return m_WritePath; }

            // Flushes and moves the finished file into place. Call once, after
            // every worker has been joined.
            bool Commit()
            {
                m_Stream.flush();
                if (!m_Stream)
                    return false;
                m_Stream.close();

                if (m_WritePath == m_FinalPath)
                    return true;

                std::error_code error;
                std::filesystem::rename(m_WritePath, m_FinalPath, error);
                return !error;
            }

            void WriteHeader(const std::string& line)
            {
                std::scoped_lock lock(m_Mutex);
                m_Stream << "# " << line << '\n';
            }

            // Writes a game's samples as one batch and reports whether more are
            // wanted. Truncates the batch at the target so runs land exactly on
            // the requested count.
            bool Write(const std::vector<Sample>& samples)
            {
                std::scoped_lock lock(m_Mutex);
                for (const Sample& sample : samples)
                {
                    if (m_Written >= m_Target)
                        return false;
                    m_Stream << sample.fen << " | " << sample.score << " | "
                             << sample.result << '\n';
                    ++m_Written;
                }
                return m_Written < m_Target;
            }

            uint64_t Written() const
            {
                std::scoped_lock lock(m_Mutex);
                return m_Written;
            }

            bool Failed()
            {
                std::scoped_lock lock(m_Mutex);
                return !m_Stream;
            }

        private:
            mutable std::mutex m_Mutex;
            std::ofstream m_Stream;
            std::filesystem::path m_FinalPath;
            std::filesystem::path m_WritePath;
            uint64_t m_Written = 0;
            const uint64_t m_Target;
        };

        // Direct-mapped table of Zobrist keys. Approximate by design: a
        // collision drops a position that was not really a duplicate, which
        // costs a sample rather than corrupting one, and the memory stays
        // bounded however long the run goes.
        class DuplicateFilter
        {
        public:
            explicit DuplicateFilter(int bits)
            {
                if (bits > 0)
                    m_Slots.assign(size_t{ 1 } << bits, 0);
            }

            bool IsNew(uint64_t key)
            {
                if (m_Slots.empty() || key == 0)
                    return true;
                const size_t slot = static_cast<size_t>(key) & (m_Slots.size() - 1);
                std::scoped_lock lock(m_Mutex);
                if (m_Slots[slot] == key)
                    return false;
                m_Slots[slot] = key;
                return true;
            }

        private:
            std::mutex m_Mutex;
            std::vector<uint64_t> m_Slots;
        };

        double ResultFromFlags(uint16_t flags)
        {
            if (flags & GameOverFlags::IS_CHECKMATE)
                return (flags & GameOverFlags::IS_WHITE_WIN) ? WhiteWin : BlackWin;
            return Drawn;
        }

        // Per-game bookkeeping for the adjudication rules.
        class Adjudicator
        {
        public:
            explicit Adjudicator(const Config& config) : m_Config(config) {}

            // Feeds one score from White's point of view. Returns true when the
            // game should stop, with the result in `result`.
            bool Observe(int whiteScore, int ply, double& result, bool& wasWin)
            {
                if (std::abs(whiteScore) >= m_Config.winAdjudicationScore)
                {
                    const int sign = whiteScore > 0 ? 1 : -1;
                    m_WinPlies = sign == m_WinSign ? m_WinPlies + 1 : 1;
                    m_WinSign = sign;
                    if (m_WinPlies >= m_Config.winAdjudicationPlies)
                    {
                        result = sign > 0 ? WhiteWin : BlackWin;
                        wasWin = true;
                        return true;
                    }
                }
                else
                {
                    m_WinPlies = 0;
                    m_WinSign = 0;
                }

                if (ply >= m_Config.drawAdjudicationMinPly &&
                    std::abs(whiteScore) <= m_Config.drawAdjudicationScore)
                {
                    if (++m_DrawPlies >= m_Config.drawAdjudicationPlies)
                    {
                        result = Drawn;
                        wasWin = false;
                        return true;
                    }
                }
                else
                {
                    m_DrawPlies = 0;
                }

                return false;
            }

        private:
            const Config& m_Config;
            int m_WinPlies = 0;
            int m_WinSign = 0;
            int m_DrawPlies = 0;
        };

        struct WorkerTotals
        {
            uint64_t games = 0;
            uint64_t whiteWins = 0;
            uint64_t blackWins = 0;
            uint64_t draws = 0;
            uint64_t adjudicatedWins = 0;
            uint64_t adjudicatedDraws = 0;
            uint64_t filtered = 0;
            uint64_t duplicates = 0;
        };

        // Plays the random opening. Returns false when it stumbled into a
        // finished position, in which case the game is discarded.
        bool PlayRandomOpening(ChessBoard& board, Random& random, int plies)
        {
            for (int ply = 0; ply < plies; ++ply)
            {
                const MoveList<218> moves = board.GetLegalMoves();
                if (moves.size() == 0)
                    return false;
                board.MakeMove(moves[random.Below(moves.size())], true);
                if (board.GetGameOver() != 0)
                    return false;
            }
            return true;
        }

        // Random play labelled with material balance. No search, no network.
        void PlayMaterialGame(const Config& config, Random& random, DuplicateFilter& duplicates,
            std::vector<Sample>& samples, WorkerTotals& totals)
        {
            samples.clear();
            ChessBoard board;

            for (int ply = 0; ply < config.maxGamePlies; ++ply)
            {
                if (board.GetGameOver() != 0)
                    break;

                const MoveList<218> moves = board.GetLegalMoves();
                if (moves.size() == 0)
                    break;

                const bool whiteToMove =
                    board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove);
                const bool inCheck = board.IsInCheck();

                if (config.skipInCheck && inCheck)
                {
                    ++totals.filtered;
                }
                else if (!duplicates.IsNew(board.GetZobristKey()))
                {
                    ++totals.duplicates;
                }
                else
                {
                    const int whiteScore = MaterialScore(board.GetBoardState());
                    const int score = whiteToMove ? whiteScore : -whiteScore;
                    samples.push_back(Sample{ board.GetFENString(), score,
                        ScoreToWinProbability(score), whiteToMove });
                }

                board.MakeMove(moves[random.Below(moves.size())], true);
            }

            ++totals.games;
            ++totals.draws;
        }

        // One self-play game with the loaded network.
        void PlaySearchGame(const Config& config, NeraChessSearch::SearchEngine& search,
            Random& random, DuplicateFilter& duplicates, std::vector<Sample>& samples,
            WorkerTotals& totals)
        {
            samples.clear();
            ChessBoard board;
            if (!PlayRandomOpening(board, random, config.randomOpeningPlies))
                return;

            NeraChessSearch::SearchLimits limits;
            limits.maxDepth = config.depth;
            limits.maxNodes = config.nodes;

            Adjudicator adjudicator(config);
            // Score of every recorded position, from White's point of view, so
            // the result can be attached once the game ends.
            std::vector<Sample> pending;
            double result = Drawn;
            bool adjudicatedWin = false;
            bool adjudicated = false;

            int ply = 0;
            for (; ply < config.maxGamePlies; ++ply)
            {
                const uint16_t over = board.GetGameOver();
                if (over != 0)
                {
                    result = ResultFromFlags(over);
                    break;
                }

                search.PrepareSearch();
                const NeraChessSearch::SearchResult searched = search.Search(board, limits);
                if (searched.bestMove == 0)
                    break;

                const bool whiteToMove =
                    board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove);
                const int whiteScore = whiteToMove ? searched.score : -searched.score;

                const bool inCheck = board.IsInCheck();
                const uint8_t moveFlags = searched.bestMove.GetMoveFlags();
                const bool tactical = (moveFlags &
                    (MoveFlags::IS_CAPTURE | MoveFlags::IS_PROMOTION)) != 0;

                const bool keep =
                    !(config.skipInCheck && inCheck) &&
                    !(config.skipCaptureBestMove && tactical) &&
                    !IsMateScore(searched.score) &&
                    std::abs(searched.score) <= config.maxAbsoluteScore;

                if (!keep)
                {
                    ++totals.filtered;
                }
                else if (!duplicates.IsNew(board.GetZobristKey()))
                {
                    ++totals.duplicates;
                }
                else
                {
                    // Result is filled in once the game finishes.
                    pending.push_back(Sample{ board.GetFENString(), searched.score, Drawn,
                        whiteToMove });
                }

                board.MakeMove(searched.bestMove, true);

                if (adjudicator.Observe(whiteScore, ply, result, adjudicatedWin))
                {
                    adjudicated = true;
                    break;
                }
            }

            if (adjudicated)
            {
                if (adjudicatedWin)
                    ++totals.adjudicatedWins;
                else
                    ++totals.adjudicatedDraws;
            }

            ++totals.games;
            if (result == WhiteWin)
                ++totals.whiteWins;
            else if (result == BlackWin)
                ++totals.blackWins;
            else
                ++totals.draws;

            // Attach the result from each recorded position's own point of view.
            samples.reserve(pending.size());
            for (Sample& sample : pending)
            {
                sample.result = sample.whiteToMove ? result : 1.0 - result;
                samples.push_back(std::move(sample));
            }
        }
    }

    std::string Statistics::Describe() const
    {
        std::ostringstream text;
        text << positionsWritten << " positions from " << gamesPlayed << " games"
             << " (" << whiteWins << " white, " << blackWins << " black, " << draws << " drawn"
             << "; " << adjudicatedWins << " adjudicated wins, "
             << adjudicatedDraws << " adjudicated draws)"
             << "; dropped " << filteredPositions << " filtered and "
             << duplicatePositions << " duplicate positions";
        return text.str();
    }

    int Run(const Config& config, Statistics& statistics, std::ostream& log)
    {
        if (config.mode == Mode::SelfPlay)
        {
            std::string message;
            if (!NeraChessSearch::Evaluation::LoadNetwork(config.networkPath, message))
            {
                log << "error: " << message << '\n'
                    << "Self-play needs a network. Generate a generation-0 network with "
                       "--mode material first.\n";
                return 1;
            }
            log << message << '\n';
        }

        SampleWriter writer(config.outputPath, config.append, config.targetPositions);
        if (!writer.IsOpen())
        {
            log << "error: could not open " << writer.WritePath() << " for writing\n";
            return 1;
        }
        writer.WriteHeader(config.mode == Mode::Material
            ? "NeraChess generation-0 material labels"
            : "NeraChess self-play data, " + config.networkPath.filename().string() +
                  ", depth " + std::to_string(config.depth));

        DuplicateFilter duplicates(config.deduplicationBits);
        std::atomic<bool> wantMore{ true };
        std::atomic<uint64_t> reportedAt{ 0 };
        std::mutex totalsMutex;
        WorkerTotals combined;

        const size_t workerCount = std::max<size_t>(1, config.threads);
        std::vector<std::jthread> workers;
        workers.reserve(workerCount);

        for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
        {
            workers.emplace_back([&, workerIndex]
            {
                Random random(config.seed + workerIndex * 0x9E3779B97F4A7C15ull + 1);
                // On the heap: a SearchEngine carries enough per-worker state
                // that a thread's default stack is not a safe place for it.
                auto search = std::make_unique<NeraChessSearch::SearchEngine>(
                    config.hashMegabytes);
                WorkerTotals totals;
                std::vector<Sample> samples;

                while (wantMore.load(std::memory_order_relaxed))
                {
                    if (config.mode == Mode::Material)
                        PlayMaterialGame(config, random, duplicates, samples, totals);
                    else
                        PlaySearchGame(config, *search, random, duplicates, samples, totals);

                    if (!samples.empty() && !writer.Write(samples))
                        wantMore.store(false, std::memory_order_relaxed);

                    const uint64_t written = writer.Written();
                    if (config.progressInterval > 0 &&
                        written / config.progressInterval > reportedAt.load() )
                    {
                        reportedAt.store(written / config.progressInterval);
                        log << written << " / " << config.targetPositions << " positions\n"
                            << std::flush;
                    }
                }

                std::scoped_lock lock(totalsMutex);
                combined.games += totals.games;
                combined.whiteWins += totals.whiteWins;
                combined.blackWins += totals.blackWins;
                combined.draws += totals.draws;
                combined.adjudicatedWins += totals.adjudicatedWins;
                combined.adjudicatedDraws += totals.adjudicatedDraws;
                combined.filtered += totals.filtered;
                combined.duplicates += totals.duplicates;
            });
        }
        workers.clear();

        statistics.positionsWritten = writer.Written();
        statistics.gamesPlayed = combined.games;
        statistics.whiteWins = combined.whiteWins;
        statistics.blackWins = combined.blackWins;
        statistics.draws = combined.draws;
        statistics.adjudicatedWins = combined.adjudicatedWins;
        statistics.adjudicatedDraws = combined.adjudicatedDraws;
        statistics.filteredPositions = combined.filtered;
        statistics.duplicatePositions = combined.duplicates;

        if (writer.Failed())
        {
            log << "error: writing " << writer.WritePath() << " failed\n";
            return 1;
        }

        // Only now does the output appear under its real name, so a run that
        // died before this point leaves nothing for the next one to mistake
        // for a finished dataset.
        if (!writer.Commit())
        {
            log << "error: could not finalize " << config.outputPath << '\n';
            return 1;
        }
        return 0;
    }
}
