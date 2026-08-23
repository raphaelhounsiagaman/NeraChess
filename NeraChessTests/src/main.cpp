#include "ChessBoard.h"
#include "Clock.h"
#include "Evaluation.h"
#include "MoveOrdering.h"
#include "NnueEvaluator.h"
#include "OpeningBook.h"
#include "SimdOps.h"
#include "SearchEngine.h"
#include "TimeManagement.h"
#include "TranspositionTable.h"
#include "Zobrist.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using NeraChessEngine::ChessBoard;

    struct PerftCase
    {
        std::string_view name;
        std::string_view fen;
        int depth;
        uint64_t expectedNodes;
    };

    uint64_t Perft(ChessBoard& board, int depth)
    {
        if (depth == 0)
            return 1;

        const auto moves = board.GetLegalMoves();
        if (depth == 1)
            return moves.size();

        uint64_t nodes = 0;
        for (const auto move : moves)
        {
            board.MakeMove(move);
            nodes += Perft(board, depth - 1);
            board.UndoMove(move);
        }
        return nodes;
    }

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }

    void TestPerft()
    {
        static constexpr PerftCase cases[] = {
            { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4'865'609 },
            { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4'085'603 },
            { "position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674'624 },
            { "position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422'333 },
            { "position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2'103'487 },
            { "position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3'894'594 },
            { "illegal en passant 1", "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1'134'888 },
            { "illegal en passant 2", "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1'015'133 },
            { "en passant check", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1'440'467 },
            { "short castle check", "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661'072 },
            { "long castle check", "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803'711 },
            { "castle rights", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1'274'206 },
            { "castle prevented", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1'720'476 },
            { "promote out of check", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3'821'001 },
            { "discovered check", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1'004'658 },
            { "promotion check", "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217'342 },
            { "underpromotion check", "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92'683 },
        };

        for (const auto& test : cases)
        {
            ChessBoard board(std::string(test.fen));
            Require(board.GetError() == 0, "perft FEN failed to parse");
            const uint64_t actual = Perft(board, test.depth);
            if (actual != test.expectedNodes)
            {
                throw std::runtime_error(
                    std::string(test.name) + ": expected " + std::to_string(test.expectedNodes) +
                    ", got " + std::to_string(actual));
            }
        }
    }

    void TestFenValidation()
    {
        static constexpr std::string_view invalidFens[] = {
            "8/8/8/8/8/8/8/4K2k w - - 0",
            "9/8/8/8/8/8/8/4K2k w - - 0 1",
            "44/8/8/8/8/8/8/4K2k w - - 0 1",
            "8/8/8/8/8/8/8/4K2k/8 w - - 0 1",
            "8/8/8/8/8/8/8/4K2k x - - 0 1",
            "8/8/8/8/8/8/8/4K2k w Z - 0 1",
            "8/8/8/8/8/8/8/4K2k w - e4 0 1",
            "8/8/8/8/8/8/8/4K2k w - - -1 1",
            "8/8/8/8/8/8/8/4K2k w - - 0 0",
            "P7/8/8/8/8/8/8/4K2k w - - 0 1",
            "7k/8/8/8/8/8/8/3KK3 w - - 0 1",
            "7k/8/8/8/3Q4/8/8/K7 w - - 0 1",
        };

        for (const auto fen : invalidFens)
        {
            ChessBoard board{ std::string(fen) };
            Require(board.GetError() != 0, "malformed FEN was accepted");
        }

        static constexpr std::string_view validFen =
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 17 42";
        ChessBoard board{ std::string(validFen) };
        Require(board.GetError() == 0 && board.GetFENString() == validFen,
            "valid FEN did not round-trip");
    }

    void TestMakeUndoInvariants()
    {
        ChessBoard board;
        const ChessBoard original = board;
        const std::string originalFen = board.GetFENString();
        const uint64_t originalKey = board.GetZobristKey();

        for (const auto move : board.GetLegalMoves())
        {
            board.MakeMove(move);
            (void)board.GetLegalMoves();
            (void)board.GetZobristKey();
            board.UndoMove(move);

            Require(board == original, "board differs after make/undo");
            Require(board.GetFENString() == originalFen, "FEN differs after make/undo");
            Require(board.GetZobristKey() == originalKey, "Zobrist key differs after make/undo");
        }
    }

    void TestTerminalPositions()
    {
        using namespace NeraChessEngine;

        ChessBoard checkmate("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
        const uint16_t mateFlags = checkmate.GetGameOver();
        Require((mateFlags & IS_CHECKMATE) != 0, "checkmate was not detected");

        ChessBoard stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        const uint16_t staleFlags = stalemate.GetGameOver();
        Require((staleFlags & IS_STALEMATE) != 0, "stalemate was not detected");

        ChessBoard insufficient("7k/8/8/8/8/8/8/K7 w - - 0 1");
        const uint16_t materialFlags = insufficient.GetGameOver();
        Require((materialFlags & IS_INSUFFICIENT_MATERIAL) != 0, "insufficient material was not detected");

        ChessBoard sameColorBishops("7k/8/8/8/5b2/8/3B4/K7 w - - 0 1");
        Require((sameColorBishops.GetGameOver() & IS_INSUFFICIENT_MATERIAL) != 0,
            "same-color bishop dead position was not detected");

        ChessBoard oppositeColorBishops("7k/8/8/8/4b3/8/3B4/K7 w - - 0 1");
        Require((oppositeColorBishops.GetGameOver() & IS_INSUFFICIENT_MATERIAL) == 0,
            "opposite-color bishops were incorrectly declared insufficient");

        ChessBoard notYetFifty("7k/8/8/8/8/8/R7/K7 w - - 99 1");
        Require((notYetFifty.GetGameOver() & IS_50MOVE_RULE) == 0, "50-move draw was declared one ply early");
        const auto quietMove = notYetFifty.GetLegalMoves()[0];
        notYetFifty.MakeMove(quietMove);
        Require((notYetFifty.GetGameOver() & IS_50MOVE_RULE) != 0, "50-move draw was not declared at 100 halfmoves");
    }

    NeraChessEngine::Move FindMove(const ChessBoard& board, std::string_view uci)
    {
        for (const auto move : board.GetLegalMoves())
        {
            if (move.ToUCI() == uci)
                return move;
        }
        throw std::runtime_error("legal move not found: " + std::string(uci));
    }

    void TestRepetition()
    {
        using namespace NeraChessEngine;

        ChessBoard board;
        static constexpr std::string_view cycle[] = { "g1f3", "g8f6", "f3g1", "f6g8" };
        for (int repetition = 0; repetition < 2; ++repetition)
        {
            for (const auto uci : cycle)
                board.MakeMove(FindMove(board, uci), true);
        }
        Require((board.GetGameOver(true) & IS_REPETITION) != 0, "threefold repetition was not detected");

        ChessBoard rights("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        static constexpr std::string_view rookCycle[] = { "h1h2", "h8h7", "h2h1", "h7h8" };
        for (int repetition = 0; repetition < 2; ++repetition)
        {
            for (const auto uci : rookCycle)
                rights.MakeMove(FindMove(rights, uci), true);
        }
        Require((rights.GetGameOver(true) & IS_REPETITION) == 0,
            "positions with different castling rights were treated as repetitions");

        ChessBoard irrelevantEp("4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1");
        ChessBoard noEp("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1");
        Require(irrelevantEp.GetZobristKey() == noEp.GetZobristKey(),
            "non-capturable en passant changed position identity");

        ChessBoard capturableEp("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
        ChessBoard capturableNoEp("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1");
        Require(capturableEp.GetZobristKey() != capturableNoEp.GetZobristKey(),
            "capturable en passant was omitted from position identity");
    }

    void TestIncrementalZobrist()
    {
        static constexpr std::string_view positions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
            "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",
        };

        for (const auto fen : positions)
        {
            ChessBoard board{ std::string(fen) };
            const uint64_t originalKey = board.GetZobristKey();
            Require(originalKey == NeraChessEngine::Zobrist::CalculateZobristKey(board),
                "initial incremental Zobrist key is incorrect");
            for (const auto move : board.GetLegalMoves())
            {
                board.MakeMove(move);
                Require(board.GetZobristKey() == NeraChessEngine::Zobrist::CalculateZobristKey(board),
                    "incremental Zobrist key differs after move");
                board.UndoMove(move);
                Require(board.GetZobristKey() == originalKey, "Zobrist key differs after undo");
            }
        }
    }

    // ChessBoard::GivesCheck answers the same question as making the move and asking
    // IsInCheck, so the two are compared exhaustively over every move of every position
    // in a small tree. The search prunes on this predicate, so a disagreement would
    // quietly discard forcing moves rather than fail loudly.
    void CompareGivesCheckAgainstMakeMove(ChessBoard& board, int depth)
    {
        for (const auto move : board.GetLegalMoves())
        {
            const bool predicted = board.GivesCheck(move);
            board.MakeMove(move);
            const bool actual = board.IsInCheck();
            Require(predicted == actual, "GivesCheck disagreed with the move generator");
            if (depth > 1)
                CompareGivesCheckAgainstMakeMove(board, depth - 1);
            board.UndoMove(move);
        }
    }

    void TestGivesCheck()
    {
        static constexpr std::string_view positions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
            // En passant, promotion and castling are the three cases where the board
            // after the move differs from "piece moves from A to B".
            "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
            "4k3/1P6/8/8/8/8/6p1/4K3 w - - 0 1",
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "8/8/8/8/1k6/8/2P5/4K2R w K - 0 1",
        };

        for (const auto fen : positions)
        {
            ChessBoard board{ std::string(fen) };
            CompareGivesCheckAgainstMakeMove(board, 3);
        }
    }

    void TestNullMoveState()
    {
        ChessBoard board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 17 42");
        const ChessBoard original = board;
        const std::string originalFen = board.GetFENString();
        const uint64_t originalKey = board.GetZobristKey();

        Require(board.MakeNullMove(), "legal null move was rejected");
        Require(!board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove),
            "null move did not toggle side to move");
        Require(!board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::CanEnPassant),
            "null move did not clear en passant");
        Require(board.GetZobristKey() == NeraChessEngine::Zobrist::CalculateZobristKey(board),
            "null move incremental key is incorrect");

        board.UndoNullMove();
        Require(board == original, "board differs after null move undo");
        Require(board.GetFENString() == originalFen, "FEN differs after null move undo");
        Require(board.GetZobristKey() == originalKey, "key differs after null move undo");
    }

    void TestTranspositionTable()
    {
        using namespace NeraChessSearch;

        TranspositionTable table(1);
        Require(table.SizeBytes() <= 1024ULL * 1024ULL, "TT exceeded requested size");
        Require(!table.Probe(0), "empty TT reported a key-zero hit");

        table.NewSearch();
        table.Store(0, 42, 8, TTBound::Exact, NeraChessEngine::Move(123));
        const std::optional<TTEntry> zeroEntry = table.Probe(0);
        Require(zeroEntry && zeroEntry->score == 42 && zeroEntry->depth == 8,
            "TT failed to store key zero");

        constexpr uint64_t key = 0x123456789ABCDEF0ULL;
        table.Store(key, 300, 12, TTBound::Exact, NeraChessEngine::Move(456));
        table.Store(key, -50, 2, TTBound::Upper, NeraChessEngine::Move(789));
        const std::optional<TTEntry> preserved = table.Probe(key);
        Require(preserved && preserved->score == 300 && preserved->depth == 12 &&
            preserved->GetBound() == TTBound::Exact,
            "shallow TT bound replaced a deep exact entry");
        Require(preserved->move == 789, "TT did not refresh the best move");

        table.Clear();
        Require(!table.Probe(key), "TT clear left a valid entry");
    }

    void TestConcurrentTranspositionTable()
    {
        using namespace NeraChessSearch;

        TranspositionTable table(1);
        table.NewSearch();
        const uint64_t clusterCount = table.SizeBytes() / 64;
        constexpr size_t WorkerCount = 8;
        constexpr int Iterations = 20'000;
        std::atomic<bool> failed{ false };
        std::atomic<uint64_t> successfulProbes{ 0 };
        std::vector<std::jthread> workers;
        workers.reserve(WorkerCount);

        for (size_t worker = 0; worker < WorkerCount; ++worker)
        {
            workers.emplace_back([&, worker]
            {
                const uint64_t key = 0x1234ULL + worker * clusterCount;
                const int score = static_cast<int>(worker * 37) - 100;
                const int depth = static_cast<int>(worker) + 1;
                const NeraChessEngine::Move move(static_cast<uint32_t>(worker + 1));
                for (int iteration = 0; iteration < Iterations; ++iteration)
                {
                    table.Store(key, score, depth, TTBound::Exact, move);
                    const std::optional<TTEntry> entry = table.Probe(key);
                    if (entry)
                    {
                        ++successfulProbes;
                        if (entry->key != key || entry->move != move ||
                            entry->score != score || entry->depth != depth ||
                            entry->GetBound() != TTBound::Exact)
                        {
                            failed = true;
                            return;
                        }
                    }
                    if ((iteration & 255) == 0)
                        static_cast<void>(table.HashFullPermill());
                }
            });
        }
        for (std::jthread& worker : workers)
            worker.join();
        Require(!failed, "concurrent TT probe accepted a torn or mismatched entry");
        Require(successfulProbes.load() > 100,
            "concurrent TT stress produced no meaningful successful probes");

        TranspositionTable sameKeyTable(1);
        sameKeyTable.NewSearch();
        constexpr uint64_t SharedKey = 0xFEDCBA9876543210ULL;
        const NeraChessEngine::Move FirstMove(101);
        const NeraChessEngine::Move SecondMove(202);
        std::atomic<uint64_t> sameKeyHits{ 0 };
        failed = false;
        workers.clear();
        for (size_t worker = 0; worker < WorkerCount; ++worker)
        {
            workers.emplace_back([&, worker]
            {
                const bool firstTuple = (worker & 1) == 0;
                const NeraChessEngine::Move move = firstTuple ? FirstMove : SecondMove;
                const int score = firstTuple ? 111 : -222;
                for (int iteration = 0; iteration < Iterations; ++iteration)
                {
                    sameKeyTable.Store(SharedKey, score, 9, TTBound::Exact, move);
                    const std::optional<TTEntry> entry = sameKeyTable.Probe(SharedKey);
                    if (!entry)
                        continue;
                    ++sameKeyHits;
                    const bool firstValid = entry->move == FirstMove && entry->score == 111;
                    const bool secondValid = entry->move == SecondMove && entry->score == -222;
                    if ((!firstValid && !secondValid) || entry->depth != 9 ||
                        entry->GetBound() != TTBound::Exact)
                    {
                        failed = true;
                        return;
                    }
                }
            });
        }
        for (std::jthread& worker : workers)
            worker.join();
        Require(!failed, "same-key TT stress accepted a cross-published payload");
        Require(sameKeyHits.load() > 100,
            "same-key TT stress produced no meaningful successful probes");
    }

    bool ContainsMove(const NeraChessEngine::MoveList<218>& moves, NeraChessEngine::Move move)
    {
        for (const auto legalMove : moves)
        {
            if (legalMove == move)
                return true;
        }
        return false;
    }

    // -- NNUE scaffolding helpers -----------------------------------------
    //
    // No trained network exists yet, so the NNUE tests pin down the machinery
    // a network will run on: the file format, the feature indexing, and the
    // agreement between a full accumulator refresh and an incremental update.
    // A synthetic network with deterministic pseudo-random weights is enough
    // for all of them, because every property checked holds for any weights.

    namespace Nnue = NeraChessNNUE;

    // Deterministic weights, identical on every platform and run. The range is
    // deliberately narrow so a 32-piece accumulator cannot overflow int16.
    std::vector<std::byte> BuildSyntheticNetwork(uint64_t seed)
    {
        std::vector<std::byte> file(
            Nnue::NetworkFormat::HeaderBytes + Nnue::Architecture::TotalParameterBytes,
            std::byte{ 0 });
        const std::span<std::byte> payload =
            std::span<std::byte>(file).subspan(Nnue::NetworkFormat::HeaderBytes);

        uint64_t state = seed;
        for (size_t index = 0; index < Nnue::Architecture::TotalParameterCount; ++index)
        {
            state = state * 6'364'136'223'846'793'005ull + 1'442'695'040'888'963'407ull;
            const auto value = static_cast<Nnue::Weight>(
                static_cast<int32_t>((state >> 33) % 121) - 60);
            Nnue::NetworkFormat::WriteWeight(payload, index, value);
        }

        Nnue::NetworkFormat::Header header =
            Nnue::NetworkFormat::Header::ForCurrentArchitecture();
        header.checksum = Nnue::NetworkFormat::Checksum(payload);
        Nnue::NetworkFormat::WriteHeader(header, file);
        return file;
    }

    // Installs a synthetic network as the process-wide evaluator, so tests that
    // need evaluation to vary between positions do not depend on a trained
    // network existing. Returns the temporary file to clean up.
    std::filesystem::path InstallSyntheticNetwork(uint64_t seed, std::string_view name)
    {
        const std::vector<std::byte> file = BuildSyntheticNetwork(seed);
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / std::string(name);
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(file.data()),
                static_cast<std::streamsize>(file.size()));
        }
        Require(Nnue::Evaluator::Load(path) == Nnue::NetworkFormat::Status::Ok,
            "the synthetic network could not be installed");
        return path;
    }

    void RemoveNetworkFile(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void TestSearchFoundations()
    {
        using namespace NeraChessEngine;
        using namespace NeraChessSearch;

        SearchEngine search(16);

        ChessBoard checkmate("7k/6Q1/6K1/8/8/8/8/8 b - - 100 1");
        SearchLimits terminalLimits;
        terminalLimits.maxDepth = 2;
        const SearchResult terminal = search.Search(checkmate, terminalLimits);
        Require(terminal.bestMove == 0, "terminal search returned a move");
        Require(terminal.score == -SCORE_MATE, "checkmate score is incorrect");

        ChessBoard mateInOne("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
        SearchLimits mateLimits;
        mateLimits.maxDepth = 2;
        const SearchResult mate = search.Search(mateInOne, mateLimits);
        Require(ContainsMove(mateInOne.GetLegalMoves(), mate.bestMove), "mate search returned an illegal move");
        mateInOne.MakeMove(mate.bestMove);
        Require((mateInOne.GetGameOver() & IS_CHECKMATE) != 0, "depth-one search missed mate in one");
        Require(mate.score >= SCORE_MATE - 1, "mate-in-one score has wrong distance");

        ChessBoard checked("7k/8/8/8/8/8/8/K6R b - - 0 1");
        SearchLimits checkedLimits;
        checkedLimits.maxDepth = 1;
        const SearchResult evasion = search.Search(checked, checkedLimits);
        Require(ContainsMove(checked.GetLegalMoves(), evasion.bestMove),
            "checked quiescence node failed to return a legal evasion");
        Require(evasion.score > -SCORE_MATE + MAX_PLY,
            "checked quiescence node was incorrectly scored as mate");

        ChessBoard start;
        const ChessBoard original = start;
        SearchLimits abortLimits;
        // The depth is deliberately out of reach rather than merely deep. These tests
        // run with no network loaded, so the NNUE evaluation is a constant, and a
        // constant evaluation plus the selective search merged in from main prunes
        // hard enough to finish twelve plies inside ten thousand nodes -- which is
        // what this ceiling used to assume it could outlast. What is under test is
        // that the node ceiling aborts the search, so the depth only has to be one
        // the ceiling is guaranteed to cut short.
        abortLimits.maxDepth = 64;
        abortLimits.maxNodes = 10'000;
        const SearchResult aborted = search.Search(start, abortLimits);
        Require(aborted.aborted, "node-limited search did not report an abort");
        Require(aborted.completedDepth > 0, "aborted search lost all completed iterations");
        Require(ContainsMove(start.GetLegalMoves(), aborted.bestMove),
            "aborted search did not retain a legal completed move");
        Require(start == original, "search mutated its input board");

        SearchLimits aspirationLimits;
        aspirationLimits.maxDepth = 4;
        int completedIterations = 0;
        aspirationLimits.iterationCallback = [&completedIterations](const SearchResult&)
        {
            ++completedIterations;
        };
        const SearchResult aspiration = search.Search(start, aspirationLimits);
        Require(aspiration.completedDepth == aspirationLimits.maxDepth && !aspiration.aborted,
            "aspiration search did not complete its requested depth");
        Require(completedIterations == aspiration.completedDepth,
            "search did not report every completed iteration");
        Require(ContainsMove(start.GetLegalMoves(), aspiration.bestMove),
            "aspiration search returned an illegal move");

        SearchLimits restrictedLimits;
        restrictedLimits.maxDepth = 3;
        restrictedLimits.rootMoves.push_back(FindMove(start, "e2e4"));
        const SearchResult restricted = search.Search(start, restrictedLimits);
        Require(restricted.bestMove == restrictedLimits.rootMoves[0],
            "root-move restriction was not honored");

        search.PrepareSearch();
        search.RequestStop();
        const SearchResult externallyStopped = search.Search(start, aspirationLimits);
        Require(externallyStopped.aborted,
            "a stop requested immediately before search was lost");
        search.NewGame();
    }

    void TestFiftyMoveTranspositions()
    {
        using namespace NeraChessSearch;

        // Both positions share a Zobrist key -- they differ only in the
        // halfmove clock -- so the first search seeds the transposition table
        // with an entry the second must refuse to trust. That only proves
        // anything if the stored score is not already the draw score, so this
        // test installs a synthetic network to make evaluation vary.
        const std::filesystem::path networkPath =
            InstallSyntheticNetwork(0x2545F4914F6CDD1Dull, "nerachess-fiftymove.nnue");

        SearchEngine search(16);
        SearchLimits limits;
        limits.maxDepth = 3;
        ChessBoard ordinary("7k/8/8/8/8/3Q4/8/K7 w - - 0 1");
        const SearchResult winning = search.Search(ordinary, limits);
        Require(winning.score != SCORE_DRAW,
            "the seeded transposition entry was already the draw score");

        ChessBoard nearDraw("7k/8/8/8/8/3Q4/8/K7 w - - 98 1");
        const SearchResult draw = search.Search(nearDraw, limits);
        Require(draw.score == SCORE_DRAW,
            "transposition score bypassed the approaching 50-move draw");

        Nnue::Evaluator::Unload();
        RemoveNetworkFile(networkPath);
    }

    void TestMultithreadedSearch()
    {
        using namespace NeraChessEngine;
        using namespace NeraChessSearch;

        // Tools that generate data or run matches construct a SearchEngine
        // inside each worker thread, and a thread gets a 512 KB stack by
        // default on macOS. When the NNUE accumulator stack was held inline
        // this crashed with SIGBUS on construction, before any search ran.
        {
            std::atomic<bool> constructed{ false };
            std::jthread worker([&constructed]
            {
                SearchEngine onThreadStack(1);
                SearchLimits limits;
                limits.maxDepth = 1;
                ChessBoard board;
                constructed = ContainsMove(board.GetLegalMoves(),
                    onThreadStack.Search(board, limits).bestMove);
            });
            worker.join();
            Require(constructed,
                "a SearchEngine could not be built and used on a worker thread's stack");
        }

        SearchEngine search(32);
        search.SetThreadCount(4);
        Require(search.GetThreadCount() == 4, "search did not retain its thread count");

        ChessBoard board("r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 9");
        const ChessBoard original = board;
        SearchLimits limits;
        limits.maxDepth = 5;
        const std::thread::id caller = std::this_thread::get_id();
        std::vector<int> callbackDepths;
        limits.iterationCallback = [&](const SearchResult& iteration)
        {
            Require(std::this_thread::get_id() == caller,
                "helper worker invoked the iteration callback");
            callbackDepths.push_back(iteration.completedDepth);
        };

        const SearchResult result = search.Search(board, limits);
        Require(result.completedDepth == limits.maxDepth && !result.aborted,
            "multithreaded search did not complete its requested depth");
        Require(ContainsMove(board.GetLegalMoves(), result.bestMove),
            "multithreaded search returned an illegal move");
        Require(!result.principalVariation.empty() &&
            result.principalVariation.front() == result.bestMove,
            "multithreaded search returned an inconsistent principal variation");
        Require(board == original, "multithreaded search mutated its input board");
        Require(callbackDepths.size() == static_cast<size_t>(result.completedDepth),
            "helper workers emitted callbacks or an iteration callback was lost");
        for (size_t index = 0; index < callbackDepths.size(); ++index)
        {
            Require(callbackDepths[index] == static_cast<int>(index + 1),
                "multithreaded iteration callbacks were out of order");
        }

        search.NewGame();
        ChessBoard mateInOne("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
        SearchLimits mateLimits;
        mateLimits.maxDepth = 2;
        const SearchResult mate = search.Search(mateInOne, mateLimits);
        mateInOne.MakeMove(mate.bestMove);
        Require((mateInOne.GetGameOver() & IS_CHECKMATE) != 0,
            "multithreaded search missed mate in one");

        search.NewGame();
        ChessBoard start;
        SearchLimits restricted;
        restricted.maxDepth = 4;
        restricted.rootMoves.push_back(FindMove(start, "e2e4"));
        Require(search.Search(start, restricted).bestMove == restricted.rootMoves.front(),
            "multithreaded search ignored its root-move restriction");

        search.NewGame();
        SearchLimits nodeLimited;
        nodeLimited.maxDepth = 30;
        nodeLimited.maxNodes = 20'000;
        const SearchResult limited = search.Search(start, nodeLimited);
        Require(limited.aborted, "aggregate multithreaded node limit did not stop search");
        Require(limited.nodes >= nodeLimited.maxNodes &&
            limited.nodes <= nodeLimited.maxNodes + search.GetThreadCount() * 256,
            "multithreaded node accounting did not enforce one aggregate budget");

        search.NewGame();
        SearchLimits stoppedLimits;
        stoppedLimits.maxDepth = 30;
        std::mutex depthMutex;
        std::condition_variable depthCondition;
        int reachedDepth = 0;
        stoppedLimits.iterationCallback = [&](const SearchResult& iteration)
        {
            {
                std::scoped_lock lock(depthMutex);
                reachedDepth = iteration.completedDepth;
            }
            depthCondition.notify_one();
        };
        SearchResult stoppedResult;
        std::jthread searching([&]
        {
            stoppedResult = search.Search(start, stoppedLimits);
        });
        std::unique_lock depthLock(depthMutex);
        const bool startedPromptly = depthCondition.wait_for(depthLock,
            std::chrono::seconds{ 2 }, [&] { return reachedDepth >= 2; });
        depthLock.unlock();
        search.RequestStop();
        searching.join();
        Require(startedPromptly, "multithreaded search did not start promptly");
        Require(stoppedResult.aborted &&
            ContainsMove(start.GetLegalMoves(), stoppedResult.bestMove),
            "external stop did not preserve a legal completed multithreaded result");

        search.NewGame();
        search.SetThreadCount(2);
        search.ResizeHash(8);
        search.SetThreadCount(1);
        Require(search.GetThreadCount() == 1,
            "search thread-count lifecycle did not return to one worker");
        SearchLimits lifecycleLimits;
        lifecycleLimits.maxDepth = 2;
        Require(ContainsMove(start.GetLegalMoves(),
            search.Search(start, lifecycleLimits).bestMove),
            "search failed after thread-count and hash reconfiguration");
    }

    void TestNnueNetworkFormat()
    {
        const std::vector<std::byte> file = BuildSyntheticNetwork(0x9E3779B97F4A7C15ull);

        Nnue::NetworkFormat::Header header;
        Require(Nnue::NetworkFormat::ReadHeader(file, header) == Nnue::NetworkFormat::Status::Ok,
            "a freshly written network did not parse");
        Require(header.MatchesCurrentArchitecture(),
            "a freshly written network did not match the compiled architecture");
        Require(header.architectureHash == Nnue::Architecture::ArchitectureHash(),
            "network header carries the wrong architecture hash");
        Require(header.parameterCount == Nnue::Architecture::TotalParameterCount,
            "network header carries the wrong parameter count");

        Nnue::Network network;
        Require(network.LoadFromMemory(file) == Nnue::NetworkFormat::Status::Ok,
            "a valid network failed to load");
        Require(network.IsLoaded(), "a loaded network does not report itself as loaded");

        // Every parameter must survive the round trip, which is what makes the
        // Python exporter's output trustworthy.
        const std::filesystem::path roundTripPath =
            std::filesystem::temp_directory_path() / "nerachess-roundtrip.nnue";
        Require(network.SaveToFile(roundTripPath) == Nnue::NetworkFormat::Status::Ok,
            "saving a loaded network failed");
        std::ifstream stream(roundTripPath, std::ios::binary);
        const std::vector<char> written{ std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>() };
        stream.close();
        std::error_code removeError;
        std::filesystem::remove(roundTripPath, removeError);
        Require(written.size() == file.size() &&
            std::memcmp(written.data(), file.data(), file.size()) == 0,
            "saving a loaded network did not reproduce the original bytes");

        const auto rejects = [&file](size_t offset, uint8_t replacement,
            Nnue::NetworkFormat::Status expected, std::string_view message)
        {
            std::vector<std::byte> damaged = file;
            damaged[offset] = static_cast<std::byte>(replacement);
            Nnue::Network candidate;
            Require(candidate.LoadFromMemory(damaged) == expected, message);
            Require(!candidate.IsLoaded(), "a rejected network was left loaded");
        };
        rejects(0, 'X', Nnue::NetworkFormat::Status::BadMagic,
            "a file with the wrong magic was accepted");
        rejects(8, 99, Nnue::NetworkFormat::Status::UnsupportedVersion,
            "a file with an unknown format version was accepted");
        rejects(20, 1, Nnue::NetworkFormat::Status::ArchitectureMismatch,
            "a file with a different hidden size was accepted");
        rejects(Nnue::NetworkFormat::HeaderBytes, 0xFF,
            Nnue::NetworkFormat::Status::ChecksumMismatch,
            "a file with corrupted weights was accepted");

        std::vector<std::byte> truncated = file;
        truncated.resize(file.size() - 2);
        Nnue::Network truncatedNetwork;
        Require(truncatedNetwork.LoadFromMemory(truncated) ==
            Nnue::NetworkFormat::Status::TruncatedPayload,
            "a truncated file was accepted");
        Require(truncatedNetwork.LoadFromMemory(
            std::span<const std::byte>(file).first(8)) ==
            Nnue::NetworkFormat::Status::TooSmall,
            "a file shorter than its header was accepted");
    }

    // ActivatedDotProduct's AVX2/SSE4.1 clones on an SSE2-baseline x86 build
    // (NeraChessNNUE/src/SimdOps.h) are runtime-multiversioned rather than
    // compile-time selected, so exercising Simd::ActivatedDotProduct alone
    // only tests whichever tier this machine happens to pick. Call every
    // tier the binary carries directly -- guarded by the same CPU-support
    // check the dispatcher itself uses, since calling an AVX2 clone on
    // hardware without AVX2 is a SIGILL, not a wrong answer.
    void RequireDispatchTiersAgree(const Nnue::Weight* values, const Nnue::Weight* weights,
        size_t length)
    {
#if defined(NNUE_SIMD_X86_DISPATCH)
        const Nnue::Accumulation reference =
            Nnue::Simd::Scalar::ActivatedDotProduct(values, weights, length);
        if (__builtin_cpu_supports("sse4.1"))
        {
            Require(Nnue::Simd::Dispatch::DotSse41(values, weights, length) == reference,
                "SIMD ActivatedDotProduct's SSE4.1 dispatch tier disagrees with the scalar "
                "reference at length " + std::to_string(length));
        }
        if (__builtin_cpu_supports("avx2"))
        {
            Require(Nnue::Simd::Dispatch::DotAvx2(values, weights, length) == reference,
                "SIMD ActivatedDotProduct's AVX2 dispatch tier disagrees with the scalar "
                "reference at length " + std::to_string(length));
        }
#else
        (void)values;
        (void)weights;
        (void)length;
#endif
    }

    void TestNnueSimdKernels()
    {
        // Vector kernels must agree with the scalar reference exactly, not
        // approximately. A one-off difference would make two search threads
        // built for different instruction sets disagree about the same
        // position and write contradictory scores into the shared table.
        //
        // The inputs deliberately include the values that break naive kernels:
        // int16 extremes, the activation's clipping ceiling and the values
        // either side of it, and weights large enough that a term times a
        // square approaches the int32 limit.
        static constexpr Nnue::Weight interesting[] = {
            0, 1, -1, 255, 256, 254, -255, 32767, -32768, 32766, -32767, 128, -128,
        };

        uint64_t state = 0x853C49E6748FEA9Bull;
        const auto next = [&state]() -> Nnue::Weight
        {
            state = state * 6'364'136'223'846'793'005ull + 1'442'695'040'888'963'407ull;
            const uint64_t draw = state >> 33;
            // Mostly ordinary magnitudes, with extremes mixed in often enough
            // to matter.
            if (draw % 4 == 0)
                return interesting[draw / 4 % std::size(interesting)];
            return static_cast<Nnue::Weight>(static_cast<int32_t>(draw % 65536) - 32768);
        };

        // Lengths that exercise whole vectors and every tail width.
        static constexpr size_t lengths[] = { 0, 1, 3, 7, 8, 15, 16, 17, 31, 33, 512 };

        for (const size_t length : lengths)
        {
            for (int trial = 0; trial < 8; ++trial)
            {
                std::vector<Nnue::Weight> values(length);
                std::vector<Nnue::Weight> added(length);
                std::vector<Nnue::Weight> removed(length);
                for (size_t index = 0; index < length; ++index)
                {
                    values[index] = next();
                    added[index] = next();
                    removed[index] = next();
                }

                const auto compare = [&](std::string_view what,
                    void (*vector)(Nnue::Weight*, const Nnue::Weight*, size_t),
                    void (*scalar)(Nnue::Weight*, const Nnue::Weight*, size_t))
                {
                    std::vector<Nnue::Weight> byVector = values;
                    std::vector<Nnue::Weight> byScalar = values;
                    vector(byVector.data(), added.data(), length);
                    scalar(byScalar.data(), added.data(), length);
                    Require(byVector == byScalar,
                        "SIMD " + std::string(what) + " disagrees with the scalar reference at length " +
                            std::to_string(length));
                };
                compare("Add", Nnue::Simd::Add, Nnue::Simd::Scalar::Add);
                compare("Subtract", Nnue::Simd::Subtract, Nnue::Simd::Scalar::Subtract);

                std::vector<Nnue::Weight> fusedVector = values;
                std::vector<Nnue::Weight> fusedScalar = values;
                Nnue::Simd::AddSubtract(fusedVector.data(), added.data(), removed.data(), length);
                Nnue::Simd::Scalar::AddSubtract(fusedScalar.data(), added.data(),
                    removed.data(), length);
                Require(fusedVector == fusedScalar,
                    "SIMD AddSubtract disagrees with the scalar reference at length " +
                        std::to_string(length));

                Require(Nnue::Simd::ActivatedDotProduct(values.data(), added.data(), length) ==
                    Nnue::Simd::Scalar::ActivatedDotProduct(values.data(), added.data(), length),
                    "SIMD ActivatedDotProduct disagrees with the scalar reference at length " +
                        std::to_string(length));

                RequireDispatchTiersAgree(values.data(), added.data(), length);
            }
        }

        // A saturated accumulator against the largest weights the format
        // permits: the worst case the output layer can ever be handed.
        std::vector<Nnue::Weight> saturated(Nnue::Architecture::HiddenSize, 32767);
        std::vector<Nnue::Weight> extremeWeights(Nnue::Architecture::HiddenSize, -32768);
        Require(Nnue::Simd::ActivatedDotProduct(saturated.data(), extremeWeights.data(),
            saturated.size()) ==
            Nnue::Simd::Scalar::ActivatedDotProduct(saturated.data(), extremeWeights.data(),
                saturated.size()),
            "SIMD ActivatedDotProduct overflows where the scalar reference does not");
        RequireDispatchTiersAgree(saturated.data(), extremeWeights.data(), saturated.size());
    }

    void TestNnueFeatureIndexing()
    {
        using namespace NeraChessEngine;

        // Indices must stay inside the input space the weight matrix covers.
        for (uint8_t piece = 0; piece < 12; ++piece)
        {
            for (uint8_t square = 0; square < 64; ++square)
            {
                for (const Nnue::Perspective perspective :
                    { Nnue::Perspective::White, Nnue::Perspective::Black })
                {
                    const Nnue::FeatureIndex index =
                        Nnue::FeatureSet::FeatureIndexOf(perspective, Piece(piece), square);
                    Require(index < Nnue::Architecture::TotalInputSize,
                        "a feature index fell outside the input space");
                }
            }
        }

        // The two perspectives of one position must differ, otherwise the
        // network sees the same input for both sides.
        ChessBoard asymmetric("4k3/8/8/8/3P4/8/8/4K3 w - - 0 1");
        Nnue::FeatureSet::ActiveFeatures white;
        Nnue::FeatureSet::ActiveFeatures black;
        Nnue::FeatureSet::CollectActiveFeatures(asymmetric.GetBoardState(),
            Nnue::Perspective::White, white);
        Nnue::FeatureSet::CollectActiveFeatures(asymmetric.GetBoardState(),
            Nnue::Perspective::Black, black);
        Require(white.count == 3 && black.count == 3,
            "active feature count does not match the piece count");
        std::vector<Nnue::FeatureIndex> whiteIndices(white.indices.begin(),
            white.indices.begin() + white.count);
        std::vector<Nnue::FeatureIndex> blackIndices(black.indices.begin(),
            black.indices.begin() + black.count);
        std::sort(whiteIndices.begin(), whiteIndices.end());
        std::sort(blackIndices.begin(), blackIndices.end());
        Require(whiteIndices != blackIndices,
            "both perspectives produced the same features for an asymmetric position");

        // A position and its colour-and-rank mirror must produce identical
        // features once each is read from its own side's perspective. This is
        // the property that lets both perspectives share one weight matrix.
        ChessBoard mirrored("4k3/8/8/3p4/8/8/8/4K3 b - - 0 1");
        Nnue::FeatureSet::ActiveFeatures mirroredBlack;
        Nnue::FeatureSet::CollectActiveFeatures(mirrored.GetBoardState(),
            Nnue::Perspective::Black, mirroredBlack);
        std::vector<Nnue::FeatureIndex> mirroredIndices(mirroredBlack.indices.begin(),
            mirroredBlack.indices.begin() + mirroredBlack.count);
        std::sort(mirroredIndices.begin(), mirroredIndices.end());
        Require(mirroredIndices == whiteIndices,
            "mirrored positions did not produce mirrored features");

        Require(!Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::White,
            Square::e1, Square::h8),
            "a king move demanded a refresh from a king-independent feature set");
    }

    void TestNnueAccumulatorUpdates()
    {
        using namespace NeraChessEngine;

        const std::vector<std::byte> file = BuildSyntheticNetwork(0xD1B54A32D192ED03ull);
        Nnue::Network network;
        Require(network.LoadFromMemory(file) == Nnue::NetworkFormat::Status::Ok,
            "the synthetic network failed to load");

        struct DeltaCase
        {
            std::string_view name;
            std::string_view fen;
            std::string_view move;
        };
        static constexpr DeltaCase cases[] = {
            { "quiet", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4" },
            { "capture", "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5" },
            { "en passant", "8/8/1k6/8/3pP3/8/6K1/8 b - e3 0 1", "d4e3" },
            { "promotion", "7k/P7/8/8/8/8/8/K7 w - - 0 1", "a7a8q" },
            { "capture promotion", "1r5k/P7/8/8/8/8/8/K7 w - - 0 1", "a7b8q" },
            { "white kingside castle", "4k3/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1" },
            { "white queenside castle", "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1" },
            { "black kingside castle", "4k2r/8/8/8/8/8/8/4K3 b k - 0 1", "e8g8" },
            { "black queenside castle", "r3k3/8/8/8/8/8/8/4K3 b q - 0 1", "e8c8" },
        };

        for (const DeltaCase& testCase : cases)
        {
            ChessBoard board{ std::string(testCase.fen) };
            const Move move = FindMove(board, testCase.move);
            Require(move != 0,
                "NNUE delta case " + std::string(testCase.name) + " has no such legal move");

            Nnue::Accumulator incremental;
            incremental.Refresh(network, board.GetBoardState());

            const Nnue::DirtyPieces dirty = Nnue::DescribeMove(board, move);
            for (const Nnue::Perspective perspective :
                { Nnue::Perspective::White, Nnue::Perspective::Black })
            {
                Nnue::FeatureSet::FeatureDelta delta;
                Nnue::FeatureSet::ComputeDelta(dirty, perspective, 0, delta);
                incremental.ApplyDelta(network, delta, perspective);
            }

            board.MakeMove(move);
            Nnue::Accumulator refreshed;
            refreshed.Refresh(network, board.GetBoardState());

            Require(incremental.values == refreshed.values,
                "NNUE delta case " + std::string(testCase.name) +
                    " diverged from a full refresh");
        }
    }

    // Walks a real move tree with the accumulator stack, requiring the
    // incrementally updated accumulator to equal a full refresh at every node.
    //
    // This is the test that matters for incremental updates. A wrong
    // dirty-piece list for some rare move -- an en-passant capture that also
    // breaks a pin, a promotion that captures a rook and cancels castling --
    // would otherwise show up only as an engine that evaluates a handful of
    // positions wrongly, with nothing pointing at the cause.
    uint64_t WalkAccumulator(ChessBoard& board, Nnue::AccumulatorStack& stack,
        const Nnue::Network& network, int depth, std::string_view name)
    {
        Nnue::Accumulator reference;
        reference.Refresh(network, board.GetBoardState());
        Require(stack.Current().computed,
            "accumulator walk over " + std::string(name) + " left an entry stale");
        Require(stack.Current().values == reference.values,
            "incremental accumulator diverged from a full refresh in " + std::string(name));

        uint64_t nodes = 1;
        if (depth == 0)
            return nodes;

        for (const NeraChessEngine::Move move : board.GetLegalMoves())
        {
            const Nnue::DirtyPieces dirty = Nnue::DescribeMove(board, move);
            board.MakeMove(move);
            stack.Push(network, board.GetBoardState(), dirty);

            nodes += WalkAccumulator(board, stack, network, depth - 1, name);

            board.UndoMove(move);
            stack.Pop();
        }
        return nodes;
    }

    void TestNnueAccumulatorStack()
    {
        struct WalkCase
        {
            std::string_view name;
            std::string_view fen;
            int depth;
        };

        // Chosen for move-type coverage rather than size: castling both ways,
        // en passant, promotions with and without capture, and rook captures
        // that change castling rights.
        static constexpr WalkCase cases[] = {
            { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3 },
            { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2 },
            { "pawn endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3 },
            { "promotions", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 2 },
            { "en passant", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 3 },
            { "castle rights", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 2 },
        };

        const std::vector<std::byte> file = BuildSyntheticNetwork(0x14057B7EF767814Full);
        Nnue::Network network;
        Require(network.LoadFromMemory(file) == Nnue::NetworkFormat::Status::Ok,
            "the synthetic network failed to load for the accumulator walk");

        for (const WalkCase& testCase : cases)
        {
            ChessBoard board{ std::string(testCase.fen) };
            Nnue::AccumulatorStack stack;
            stack.Reset(network, board.GetBoardState());

            WalkAccumulator(board, stack, network, testCase.depth, testCase.name);

            Require(stack.Depth() == 0,
                "accumulator stack did not unwind after walking " +
                    std::string(testCase.name));
        }
    }

    void TestNnueEvaluation()
    {
        using NeraChessSearch::SearchEngine;

        // With no network loaded every position must evaluate to the documented
        // constant rather than to stale or uninitialized values.
        Nnue::Evaluator::Unload();
        Require(!NeraChessSearch::Evaluation::IsNetworkLoaded(),
            "the evaluator reported a network after being unloaded");
        ChessBoard start;
        ChessBoard lopsided("4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1");
        Require(SearchEngine::Evaluate(start) == Nnue::Evaluator::NoNetworkScore &&
            SearchEngine::Evaluate(lopsided) == Nnue::Evaluator::NoNetworkScore,
            "evaluation without a network is not the documented constant");

        // Load a synthetic network through the real file path, as the engine
        // does at startup.
        const std::filesystem::path networkPath =
            InstallSyntheticNetwork(0xA24BAED4963EE407ull, "nerachess-synthetic.nnue");
        Require(NeraChessSearch::Evaluation::IsNetworkLoaded(),
            "the evaluator did not report a loaded network");

        // Evaluation must be a pure function of the position.
        Require(SearchEngine::Evaluate(start) == SearchEngine::Evaluate(start),
            "evaluation is not deterministic");

        // A position and its colour-and-rank mirror must evaluate identically,
        // for any weights, because their perspective features are mirrored.
        ChessBoard original("4k3/8/8/8/3P4/8/8/4K3 w - - 0 1");
        ChessBoard mirrored("4k3/8/8/3p4/8/8/8/4K3 b - - 0 1");
        Require(SearchEngine::Evaluate(original) == SearchEngine::Evaluate(mirrored),
            "evaluation is not colour-and-rank symmetric");

        // The search's accumulator path and the standalone path must agree.
        Nnue::Accumulator accumulator;
        Require(NeraChessSearch::Evaluation::Evaluate(original.GetBoardState(), accumulator) ==
            SearchEngine::Evaluate(original),
            "the accumulator and scratch evaluation paths disagree");
        Require(accumulator.computed,
            "evaluating through an accumulator left it marked stale");

        // Searches must return legal moves with a network loaded, even one that
        // knows nothing about chess. In Debug builds NNUE_VERIFY_ACCUMULATOR
        // re-derives every accumulator these searches touch and asserts it
        // matches a full refresh, so this also covers the search's own
        // make/unmake paths -- quiescence captures and all.
        static constexpr std::string_view searchPositions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
            "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
        };

        NeraChessSearch::SearchEngine search(8);
        NeraChessSearch::SearchLimits limits;
        limits.maxDepth = 5;
        for (const std::string_view fen : searchPositions)
        {
            search.NewGame();
            ChessBoard position{ std::string(fen) };
            Require(ContainsMove(position.GetLegalMoves(),
                search.Search(position, limits).bestMove),
                "search with a loaded network did not return a legal move");
        }

        // Multithreaded searches keep one accumulator stack per worker; a
        // shared one would corrupt every helper the moment two threads made
        // different moves.
        search.NewGame();
        search.SetThreadCount(3);
        ChessBoard threaded("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        Require(ContainsMove(threaded.GetLegalMoves(),
            search.Search(threaded, limits).bestMove),
            "multithreaded search with a loaded network did not return a legal move");
        search.SetThreadCount(1);

        // A network that fails to load must leave the working one in place. A
        // mistyped EvalFile used to unload the live network and leave the
        // engine evaluating every position as zero, with no way back short of
        // a restart.
        const auto before = SearchEngine::Evaluate(original);
        Require(Nnue::Evaluator::Load("nerachess-no-such-network.nnue") !=
            Nnue::NetworkFormat::Status::Ok,
            "loading a missing network reported success");
        Require(NeraChessSearch::Evaluation::IsNetworkLoaded(),
            "a failed load unloaded the working network");
        Require(SearchEngine::Evaluate(original) == before,
            "a failed load changed what the working network evaluates");

        // The same must hold for a file that exists but is not a network.
        const std::filesystem::path corrupt =
            std::filesystem::temp_directory_path() / "nerachess-corrupt.nnue";
        {
            std::ofstream stream(corrupt, std::ios::binary | std::ios::trunc);
            stream << "this is not a network";
        }
        Require(Nnue::Evaluator::Load(corrupt) != Nnue::NetworkFormat::Status::Ok,
            "loading a corrupt network reported success");
        Require(NeraChessSearch::Evaluation::IsNetworkLoaded() &&
            SearchEngine::Evaluate(original) == before,
            "a corrupt network replaced the working one");
        RemoveNetworkFile(corrupt);

        Nnue::Evaluator::Unload();
        RemoveNetworkFile(networkPath);
    }

    // Set by --eval-file. TestNnueEvaluation ends by unloading the synthetic
    // network it installed, so a real network has to be loaded after it and
    // before the tests that measure what the evaluator actually chooses.
    std::filesystem::path g_EvalFilePath;

    void LoadRequestedNetwork()
    {
        if (g_EvalFilePath.empty())
            return;

        std::string message;
        if (!NeraChessSearch::Evaluation::LoadNetwork(g_EvalFilePath, message))
            throw std::runtime_error("--eval-file " + g_EvalFilePath.string() + ": " + message);
        std::cout << "Loaded " << g_EvalFilePath << ": " << message << '\n';
    }

    void TestSearchChoices()
    {
        using namespace NeraChessSearch;

        // These positions measure evaluation quality, so they only mean
        // anything once a trained network exists. Until then the search has
        // nothing positional to reason about and the expected moves are
        // arbitrary.
        //
        // Run with --eval-file to point this at a real network; CI runs the
        // shipped one so these assertions cover what actually ships rather
        // than only the synthetic weights the format tests use.
        if (!Evaluation::IsNetworkLoaded())
        {
            std::cout << "Skipping strategic and tactical search choices: "
                      << "no NNUE network is loaded.\n"
                      << "Pass --eval-file <network.nnue> to run them.\n";
            return;
        }

        SearchEngine search(32);
        SearchLimits limits;
        limits.maxDepth = 6;

        // Forced mates behind a quiet sacrifice. These are the positions a selective
        // search gets wrong first: the key move is quiet, orders badly, and is exactly
        // what late-move and futility pruning discard. Asserting on the mate score as
        // well as the move keeps the test about the search rather than about taste.
        struct MateCase
        {
            std::string_view fen;
            std::string_view move;
            int mateInPlies;
        };
        static constexpr MateCase mates[] = {
            { "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", "g3g6", 3 },
            { "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - 0 1", "d6d1", 5 },
            { "r5rk/5p1p/5R2/4B3/8/8/7P/7K w - - 0 1", "f6a6", 5 },
        };

        for (const MateCase& mate : mates)
        {
            search.NewGame();
            ChessBoard board{ std::string(mate.fen) };
            SearchLimits mateLimits;
            mateLimits.maxDepth = mate.mateInPlies + 3;
            const SearchResult result = search.Search(board, mateLimits);
            Require(result.bestMove == FindMove(board, mate.move),
                "search missed a forced mate behind a quiet move");
            Require(result.score >= SCORE_MATE - MAX_PLY,
                "search found the mating move without a mate score");
        }

        search.NewGame();
        ChessBoard tactical("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        const auto bestMove = search.Search(tactical, limits).bestMove;
        Require(bestMove == FindMove(tactical, "d5e6") || bestMove == FindMove(tactical, "e2a6"),
            "search missed both top tactical continuations in the benchmark");
    }

    // Writes a network of deterministic pseudo-random weights.
    //
    // The network knows nothing about chess and its evaluations are nonsense,
    // but it exercises every path a real network will: loading, the feature
    // transformer, incremental updates, and the output layer. Equivalent to
    // NNUETraining/scripts/make_random_network.py, without needing Python.
    int WriteRandomNetwork(const std::filesystem::path& path, uint64_t seed)
    {
        const std::vector<std::byte> file = BuildSyntheticNetwork(seed);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            std::cerr << "could not open " << path << " for writing\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
        if (!stream)
        {
            std::cerr << "could not write " << path << '\n';
            return 1;
        }

        Nnue::NetworkFormat::Header header;
        Nnue::NetworkFormat::ReadHeader(file, header);
        std::cout << "wrote " << path << " (" << header.Describe() << ", seed 0x"
                  << std::hex << seed << std::dec << ")\n";
        return 0;
    }

    // Walks a move tree evaluating at every node, either updating accumulators
    // incrementally or forcing a full refresh at each one. Same tree and same
    // node count either way, so the two timings differ only in accumulator
    // strategy.
    uint64_t BenchmarkWalk(ChessBoard& board, Nnue::AccumulatorStack& stack,
        const Nnue::Network& network, int depth, bool incremental, int64_t& sink)
    {
        sink += NeraChessSearch::Evaluation::Evaluate(board.GetBoardState(), stack.Current());

        uint64_t nodes = 1;
        if (depth == 0)
            return nodes;

        for (const NeraChessEngine::Move move : board.GetLegalMoves())
        {
            if (incremental)
            {
                const Nnue::DirtyPieces dirty = Nnue::DescribeMove(board, move);
                board.MakeMove(move);
                stack.Push(network, board.GetBoardState(), dirty);
            }
            else
            {
                board.MakeMove(move);
                stack.PushStale();
            }

            nodes += BenchmarkWalk(board, stack, network, depth - 1, incremental, sink);

            board.UndoMove(move);
            stack.Pop();
        }
        return nodes;
    }

    void RunNnueBenchmark()
    {
        struct BenchCase
        {
            std::string_view name;
            std::string_view fen;
            int depth;
        };
        static constexpr BenchCase cases[] = {
            { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3 },
            { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3 },
            { "endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4 },
        };

        const std::filesystem::path networkPath =
            InstallSyntheticNetwork(0x3C6EF372FE94F82Bull, "nerachess-bench.nnue");
        const Nnue::Network& network = Nnue::Evaluator::GetNetwork();

        std::cout << "NNUE evaluation benchmark (" << network.GetHeader().Describe() << ")\n"
                  << "kernels " << Nnue::Simd::TargetName() << "\n\n";

        for (const BenchCase& testCase : cases)
        {
            double timings[2] = { 0.0, 0.0 };
            uint64_t nodeCount = 0;
            int64_t sink = 0;

            for (int pass = 0; pass < 2; ++pass)
            {
                const bool incremental = pass == 1;
                ChessBoard board{ std::string(testCase.fen) };
                Nnue::AccumulatorStack stack;
                stack.Reset(network, board.GetBoardState());

                const auto started = std::chrono::steady_clock::now();
                nodeCount = BenchmarkWalk(board, stack, network, testCase.depth,
                    incremental, sink);
                timings[pass] = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
            }

            const double refreshRate = nodeCount / std::max(1e-9, timings[0]);
            const double incrementalRate = nodeCount / std::max(1e-9, timings[1]);
            std::cout << testCase.name
                      << ": depth " << testCase.depth
                      << " nodes " << nodeCount
                      << " | refresh " << static_cast<uint64_t>(refreshRate) << " eval/s"
                      << " | incremental " << static_cast<uint64_t>(incrementalRate) << " eval/s"
                      << " | speedup " << (incrementalRate / std::max(1e-9, refreshRate)) << "x\n";
            // Keeps the optimizer from deleting the evaluations entirely.
            if (sink == 0x7FFFFFFFFFFFFFFFll)
                std::cout << "";
        }

        Nnue::Evaluator::Unload();
        RemoveNetworkFile(networkPath);
    }

    // Dumps the C++ feature indices for a fixed set of positions as JSON, so
    // NNUETraining/tests/test_features.py can prove its Python indexer agrees.
    // Regenerate NNUETraining/tests/feature_vectors.json from this whenever the
    // feature set changes.
    void PrintNnueFeatureVectors()
    {
        static constexpr std::string_view fens[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1",
            "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        };

        std::cout << "{\n  \"architectureHash\": "
                  << Nnue::Architecture::ArchitectureHash()
                  << ",\n  \"perspectiveInputSize\": "
                  << Nnue::Architecture::PerspectiveInputSize
                  << ",\n  \"positions\": [\n";
        bool firstPosition = true;
        for (const std::string_view fen : fens)
        {
            ChessBoard board{ std::string(fen) };
            for (const auto& [name, perspective] : {
                std::pair<std::string_view, Nnue::Perspective>{ "white", Nnue::Perspective::White },
                std::pair<std::string_view, Nnue::Perspective>{ "black", Nnue::Perspective::Black } })
            {
                Nnue::FeatureSet::ActiveFeatures active;
                Nnue::FeatureSet::CollectActiveFeatures(board.GetBoardState(), perspective, active);
                std::vector<Nnue::FeatureIndex> indices(active.indices.begin(),
                    active.indices.begin() + active.count);
                std::sort(indices.begin(), indices.end());

                if (!firstPosition)
                    std::cout << ",\n";
                firstPosition = false;
                std::cout << "    { \"fen\": \"" << fen << "\", \"perspective\": \"" << name
                          << "\", \"features\": [";
                for (size_t index = 0; index < indices.size(); ++index)
                    std::cout << (index == 0 ? "" : ", ") << indices[index];
                std::cout << "] }";
            }
        }
        std::cout << "\n  ]\n}\n";
    }

    // The magic tables are generated from the ray-walking functions, so the constant-time
    // lookups must agree with them for every occupancy. Move ordering and evaluation both
    // read the tables directly, so a mismatch would be silent.
    void TestSliderAttackLookups()
    {
        using NeraChessEngine::Bitboard;
        using NeraChessEngine::MoveGenerator;

        uint64_t state = 0x9E3779B97F4A7C15ULL;
        const auto nextRandom = [&state]()
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };

        for (uint8_t square = 0; square < 64; ++square)
        {
            for (int trial = 0; trial < 64; ++trial)
            {
                // Sparse occupancies resemble real positions more than uniform bits do.
                const Bitboard occupancy = nextRandom() & nextRandom() & nextRandom();
                Require(MoveGenerator::LookupRookAttacks(square, occupancy) ==
                        MoveGenerator::CalculatePossibleRookMoves(square, occupancy),
                    "magic rook lookup disagrees with the ray walk");
                Require(MoveGenerator::LookupBishopAttacks(square, occupancy) ==
                        MoveGenerator::CalculatePossibleBishopMoves(square, occupancy),
                    "magic bishop lookup disagrees with the ray walk");
            }
        }
    }

    void TestStaticExchangeEvaluation()
    {
        using NeraChessSearch::MoveOrdering::StaticExchangeEvaluation;

        ChessBoard winsQueen("7k/8/8/4q3/3P4/8/8/K7 w - - 0 1");
        Require(StaticExchangeEvaluation(winsQueen, FindMove(winsQueen, "d4e5")) == 900,
            "SEE did not value an undefended queen capture");

        ChessBoard losesQueen("7k/8/5p2/4p3/3Q4/8/8/K7 w - - 0 1");
        Require(StaticExchangeEvaluation(losesQueen, FindMove(losesQueen, "d4e5")) == -800,
            "SEE did not account for a pawn recapture");

        ChessBoard enPassant("7k/8/8/3pP3/8/8/8/K7 w - d6 0 1");
        Require(StaticExchangeEvaluation(enPassant, FindMove(enPassant, "e5d6")) == 100,
            "SEE did not value an en passant capture");

        ChessBoard promotes("7k/P7/8/8/8/8/8/K7 w - - 0 1");
        Require(StaticExchangeEvaluation(promotes, FindMove(promotes, "a7a8q")) == 800,
            "SEE did not include promotion material");
    }

    void TestClockAndTimeManagement()
    {
        using namespace std::chrono;
        using NeraChessEngine::Clock;
        using NeraChessEngine::MainTimeControls;
        using NeraChessEngine::TimeControl;
        using NeraChessSearch::TimeManagement::CalculateLimits;

        ChessBoard board;
        Require(MainTimeControls.size() == 5,
            "main time-control preset count is incorrect");
        Require(MainTimeControls[0].timeControl == TimeControl{ minutes{ 1 }, milliseconds{ 0 } } &&
            MainTimeControls[1].timeControl == TimeControl{ minutes{ 3 }, seconds{ 2 } } &&
            MainTimeControls[2].timeControl == TimeControl{ minutes{ 10 }, milliseconds{ 0 } } &&
            MainTimeControls[3].timeControl == TimeControl{ minutes{ 15 }, seconds{ 10 } } &&
            MainTimeControls[4].timeControl == TimeControl{ minutes{ 90 }, seconds{ 40 } },
            "main time-control presets are incorrect");

        Clock defaultClock;
        const auto defaultLimits = CalculateLimits(board, defaultClock);
        Require(defaultLimits.softTime == seconds{ 10 },
            "default time manager soft limit is incorrect");
        Require(defaultLimits.hardTime == seconds{ 15 },
            "default time manager hard limit is incorrect");

        Clock lowClock(milliseconds{ 1'000 }, milliseconds{ 100 });
        const auto lowLimits = CalculateLimits(board, lowClock);
        Require(lowLimits.softTime >= milliseconds{ 20 } &&
            lowLimits.hardTime >= lowLimits.softTime &&
            lowLimits.hardTime < milliseconds{ 1'000 },
            "low-time search limits do not preserve a clock safety reserve");

        Clock incrementClock(seconds{ 10 }, seconds{ 1 });
        incrementClock.Start();
        incrementClock.Pause();
        const auto paused = incrementClock.GetRemaining(true);
        Require(paused <= seconds{ 10 } && paused >= milliseconds{ 9'900 },
            "clock did not account for elapsed active time");
        Require(incrementClock.Press(), "clock rejected a valid press");
        Require(!incrementClock.IsWhiteActive(), "clock press did not switch sides");
        Require(incrementClock.GetRemaining(true) >= paused + milliseconds{ 999 },
            "clock press did not add the increment");
        incrementClock.Resume();
        incrementClock.Stop();

        Clock expiredClock(milliseconds{ 0 }, seconds{ 2 });
        expiredClock.Start();
        Require(expiredClock.HasExpired(true), "zero-time clock did not expire");
        Require(!expiredClock.Press(), "increment rescued a player after flag fall");
        const auto expiredSnapshot = expiredClock.GetSnapshot();
        Require(expiredSnapshot.whiteRemaining == milliseconds{ 0 } &&
            expiredSnapshot.whiteActive,
            "expired clock switched sides or changed its remaining time");

        Clock blackToMove(seconds{ 5 });
        blackToMove.Start(false);
        const auto blackSnapshot = blackToMove.GetSnapshot();
        Require(!blackSnapshot.whiteActive && blackSnapshot.running && !blackSnapshot.paused,
            "clock did not start for the requested side");
        blackToMove.Stop();
    }

    void TestOpeningBook()
    {
        NeraChessSearch::OpeningBook book(
            "NeraChessApp/Resources/OpeningBook/OpeningBook.txt");
        Require(book.IsAvailable() && book.EntryCount() == 758'448,
            "opening-book index did not load every entry");

        ChessBoard initial("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 37 99");
        Require(book.FindMove(initial).ToUCI() == "e2e4",
            "opening-book index missed a position with transposed move counters");

        ChessBoard blackPromotion("6Q1/8/4K2P/8/1k6/p7/Bbp5/8 b - - 12 83");
        Require(book.FindMove(blackPromotion).ToUCI() == "c2c1q",
            "opening book did not match a black promotion");

        ChessBoard absent("7k/8/8/8/8/8/8/K7 w - - 0 1");
        Require(book.FindMove(absent) == 0, "opening book returned a move for an absent position");
    }

    void RunBenchmark()
    {
        ChessBoard board;
        const auto start = std::chrono::steady_clock::now();
        const uint64_t nodes = Perft(board, 6);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double seconds = std::chrono::duration<double>(elapsed).count();
        const uint64_t nps = static_cast<uint64_t>(nodes / seconds);
        std::cout << "bench nodes " << nodes << " time " << seconds << " nps " << nps << '\n';
    }

    void RunSearchBenchmark()
    {
        using namespace NeraChessSearch;

        static constexpr std::string_view positions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 9",
        };

        SearchEngine search(64);
        SearchLimits limits;
        limits.maxDepth = 6;
        for (const auto fen : positions)
        {
            search.NewGame();
            ChessBoard board{ std::string(fen) };
            const SearchResult result = search.Search(board, limits);
            const double seconds = std::max(0.001,
                std::chrono::duration<double>(result.elapsed).count());
            std::cout << "searchbench depth " << result.completedDepth
                      << " seldepth " << result.selectiveDepth
                      << " score " << result.score
                      << " bestmove " << result.bestMove.ToUCI()
                      << " nodes " << result.nodes
                      << " time " << result.elapsed.count()
                      << " nps " << static_cast<uint64_t>(result.nodes / seconds)
                      << '\n';
        }
    }

    void RunThreadBenchmark()
    {
        using namespace NeraChessSearch;

        static constexpr std::string_view positions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 9",
        };

        for (const size_t threadCount : { 1ULL, 2ULL, 4ULL })
        {
            SearchEngine search(256);
            search.SetThreadCount(threadCount);
            for (size_t positionIndex = 0; positionIndex < std::size(positions); ++positionIndex)
            {
                search.NewGame();
                ChessBoard board{ std::string(positions[positionIndex]) };
                SearchLimits limits;
                limits.maxDepth = MAX_PLY - 1;
                limits.softTime = std::chrono::milliseconds{ 750 };
                limits.hardTime = std::chrono::milliseconds{ 800 };
                const SearchResult result = search.Search(board, limits);
                const double seconds = std::max(0.001,
                    std::chrono::duration<double>(result.elapsed).count());
                std::cout << "threadbench threads " << threadCount
                          << " position " << positionIndex
                          << " depth " << result.completedDepth
                          << " seldepth " << result.selectiveDepth
                          << " score " << result.score
                          << " bestmove " << result.bestMove.ToUCI()
                          << " nodes " << result.nodes
                          << " time " << result.elapsed.count()
                          << " nps " << static_cast<uint64_t>(result.nodes / seconds)
                          << '\n';
            }
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--bench")
        {
            RunBenchmark();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--search-bench")
        {
            RunSearchBenchmark();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--thread-bench")
        {
            RunThreadBenchmark();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--nnue-feature-vectors")
        {
            PrintNnueFeatureVectors();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--nnue-bench")
        {
            RunNnueBenchmark();
            return 0;
        }
        if (argc >= 3 && std::string_view(argv[1]) == "--write-random-network")
        {
            const uint64_t seed = argc >= 4
                ? std::strtoull(argv[3], nullptr, 0)
                : 0x9E3779B97F4A7C15ull;
            return WriteRandomNetwork(argv[2], seed);
        }
        if (argc >= 2 && std::string_view(argv[1]) == "--eval-file")
        {
            if (argc < 3)
            {
                std::cerr << "--eval-file needs a path to a .nnue network\n";
                return 2;
            }
            g_EvalFilePath = argv[2];
        }

        TestFenValidation();
        TestPerft();
        TestMakeUndoInvariants();
        TestTerminalPositions();
        TestRepetition();
        TestIncrementalZobrist();
        TestGivesCheck();
        TestNullMoveState();
        TestTranspositionTable();
        TestConcurrentTranspositionTable();
        TestSearchFoundations();
        TestFiftyMoveTranspositions();
        TestMultithreadedSearch();
        TestNnueSimdKernels();
        TestNnueNetworkFormat();
        TestNnueFeatureIndexing();
        TestNnueAccumulatorUpdates();
        TestNnueAccumulatorStack();
        TestNnueEvaluation();
        LoadRequestedNetwork();
        TestSearchChoices();
        TestSliderAttackLookups();
        TestStaticExchangeEvaluation();
        TestClockAndTimeManagement();
        TestOpeningBook();
        std::cout << "All NeraChess engine tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
