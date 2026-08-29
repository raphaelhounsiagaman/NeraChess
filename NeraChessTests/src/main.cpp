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
#include <set>
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
        Require(checkmate.IsInCheck(), "fast IsInCheck missed the checking side of a checkmate");

        ChessBoard stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        const uint16_t staleFlags = stalemate.GetGameOver();
        Require((staleFlags & IS_STALEMATE) != 0, "stalemate was not detected");
        Require(!stalemate.IsInCheck(), "fast IsInCheck flagged a stalemate as check");
        Require(!stalemate.IsRuleDraw(),
            "IsRuleDraw fired on stalemate, which needs move generation to detect");

        ChessBoard insufficient("7k/8/8/8/8/8/8/K7 w - - 0 1");
        const uint16_t materialFlags = insufficient.GetGameOver();
        Require((materialFlags & IS_INSUFFICIENT_MATERIAL) != 0, "insufficient material was not detected");
        Require(insufficient.IsRuleDraw(), "IsRuleDraw missed insufficient material");

        ChessBoard sameColorBishops("7k/8/8/8/5b2/8/3B4/K7 w - - 0 1");
        Require((sameColorBishops.GetGameOver() & IS_INSUFFICIENT_MATERIAL) != 0,
            "same-color bishop dead position was not detected");
        Require(sameColorBishops.IsRuleDraw(), "IsRuleDraw missed a same-color-bishop dead position");

        ChessBoard oppositeColorBishops("7k/8/8/8/4b3/8/3B4/K7 w - - 0 1");
        Require((oppositeColorBishops.GetGameOver() & IS_INSUFFICIENT_MATERIAL) == 0,
            "opposite-color bishops were incorrectly declared insufficient");
        Require(!oppositeColorBishops.IsRuleDraw(),
            "IsRuleDraw incorrectly declared opposite-color bishops insufficient");

        ChessBoard notYetFifty("7k/8/8/8/8/8/R7/K7 w - - 99 1");
        Require((notYetFifty.GetGameOver() & IS_50MOVE_RULE) == 0, "50-move draw was declared one ply early");
        Require(!notYetFifty.IsRuleDraw(), "IsRuleDraw fired on the 50-move rule one ply early");
        const auto quietMove = notYetFifty.GetLegalMoves()[0];
        notYetFifty.MakeMove(quietMove);
        Require((notYetFifty.GetGameOver() & IS_50MOVE_RULE) != 0, "50-move draw was not declared at 100 halfmoves");
        Require(notYetFifty.IsRuleDraw(), "IsRuleDraw did not fire at 100 halfmoves");
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
        Require(board.IsRuleDraw(), "IsRuleDraw missed threefold repetition");

        ChessBoard rights("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        static constexpr std::string_view rookCycle[] = { "h1h2", "h8h7", "h2h1", "h7h8" };
        for (int repetition = 0; repetition < 2; ++repetition)
        {
            for (const auto uci : rookCycle)
                rights.MakeMove(FindMove(rights, uci), true);
        }
        Require((rights.GetGameOver(true) & IS_REPETITION) == 0,
            "positions with different castling rights were treated as repetitions");
        Require(!rights.IsRuleDraw(),
            "IsRuleDraw treated positions with different castling rights as repetitions");

        ChessBoard irrelevantEp("4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1");
        ChessBoard noEp("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1");
        Require(irrelevantEp.GetZobristKey() == noEp.GetZobristKey(),
            "non-capturable en passant changed position identity");

        ChessBoard capturableEp("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
        ChessBoard capturableNoEp("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1");
        Require(capturableEp.GetZobristKey() != capturableNoEp.GetZobristKey(),
            "capturable en passant was omitted from position identity");

        // GetRepetitionPlies / RepeatsWithin are the primitives the search uses to
        // detect an in-tree twofold before a game-rule threefold (see issue #18).
        ChessBoard twofold;
        Require(twofold.GetRepetitionPlies() == 1,
            "a freshly constructed board did not record its own position");

        twofold.MakeMove(FindMove(twofold, "g1f3"), true);
        twofold.MakeMove(FindMove(twofold, "g8f6"), true);
        twofold.MakeMove(FindMove(twofold, "f3g1"), true);
        Require(!twofold.RepeatsWithin(2),
            "RepeatsWithin fired before the cycle had actually repeated");

        twofold.MakeMove(FindMove(twofold, "f6g8"), true);
        Require(twofold.GetRepetitionPlies() == 5,
            "GetRepetitionPlies did not track four moves on top of the starting position");
        Require(twofold.RepeatsWithin(4),
            "RepeatsWithin missed a position repeating exactly at its distance bound");
        Require(!twofold.RepeatsWithin(2),
            "RepeatsWithin found a repetition outside the distance it was given");
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

    // ChessBoard::IsInCheck() is an attack-table lookup that answers the same
    // question as the move generator's own InCheck() flag, without generating the
    // legal move list. The search relies on the two never disagreeing -- a
    // disagreement would silently break checkmate detection or the pruning that
    // is gated on being in check -- so it is compared against
    // IsInCheckByMoveGeneration() (the old, move-generation-based implementation,
    // kept only for this) over every position of a small tree.
    void CompareFastInCheckAgainstMoveGeneration(ChessBoard& board, int depth)
    {
        Require(board.IsInCheck() == board.IsInCheckByMoveGeneration(),
            "fast IsInCheck disagreed with the move-generation reference");
        if (depth <= 0)
            return;
        for (const auto move : board.GetLegalMoves())
        {
            board.MakeMove(move);
            Require(board.IsInCheck() == board.IsInCheckByMoveGeneration(),
                "fast IsInCheck disagreed with the move-generation reference");
            CompareFastInCheckAgainstMoveGeneration(board, depth - 1);
            board.UndoMove(move);
        }
    }

    void TestFastInCheck()
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
            "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1",
            "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
        };

        for (const auto fen : positions)
        {
            ChessBoard board{ std::string(fen) };
            CompareFastInCheckAgainstMoveGeneration(board, 3);
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

        // Horizontal mirroring changed what a feature index means without
        // changing a single dimension, so every size field of a network
        // trained before it still matches this build's. The architecture hash
        // is the only thing that tells the two apart, and it has to: loading
        // those weights under the new feature numbering would evaluate
        // plausible-looking nonsense. King buckets did move dimensions, so
        // the sizes would catch a pre-bucket network on their own -- but the
        // hash is still what such a file carries, and pinning both values is
        // what proves no past generation can be read as the present one.
        static constexpr uint32_t PreMirroringArchitectureHash = 1'407'766'679u;
        static constexpr uint32_t PreKingBucketArchitectureHash = 1'184'502'749u;
        static_assert(PreMirroringArchitectureHash != Nnue::Architecture::ArchitectureHash(),
            "the feature set changed but the architecture hash did not; networks "
            "trained under the previous feature semantics would still load");
        static_assert(PreKingBucketArchitectureHash != Nnue::Architecture::ArchitectureHash(),
            "the feature set changed but the architecture hash did not; networks "
            "trained under the previous feature semantics would still load");
        for (const uint32_t previousHash :
            { PreMirroringArchitectureHash, PreKingBucketArchitectureHash })
        {
            std::vector<std::byte> previousFeatureSet = file;
            for (size_t byte = 0; byte < sizeof(uint32_t); ++byte)
            {
                previousFeatureSet[12 + byte] = static_cast<std::byte>(
                    (previousHash >> (byte * 8)) & 0xFFu);
            }
            Nnue::Network candidate;
            Require(candidate.LoadFromMemory(previousFeatureSet) ==
                Nnue::NetworkFormat::Status::ArchitectureMismatch,
                "a network built for an older feature set was accepted");
            Require(!candidate.IsLoaded(), "a rejected network was left loaded");
        }

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

    // Same idea as RequireDispatchTiersAgree, for the AVX2 dispatch clones of
    // the accumulator kernels (Add/Subtract/AddSubtract and the Copy*
    // variants): on an SSE2-baseline x86 build these are runtime-selected, so
    // exercising Simd::Add et al. alone only tests whichever tier this
    // machine happens to pick.
    void RequireAccumulatorDispatchTierAgrees(const Nnue::Weight* accumulator,
        const Nnue::Weight* added, const Nnue::Weight* removed, size_t length)
    {
#if defined(NNUE_SIMD_X86_DISPATCH)
        if (!__builtin_cpu_supports("avx2"))
            return;

        std::vector<Nnue::Weight> byDispatch(accumulator, accumulator + length);
        std::vector<Nnue::Weight> byScalar(accumulator, accumulator + length);
        Nnue::Simd::Dispatch::AddAvx2(byDispatch.data(), added, length);
        Nnue::Simd::Scalar::Add(byScalar.data(), added, length);
        Require(byDispatch == byScalar,
            "SIMD Add's AVX2 dispatch tier disagrees with the scalar reference at length " +
                std::to_string(length));

        byDispatch.assign(accumulator, accumulator + length);
        byScalar.assign(accumulator, accumulator + length);
        Nnue::Simd::Dispatch::SubtractAvx2(byDispatch.data(), added, length);
        Nnue::Simd::Scalar::Subtract(byScalar.data(), added, length);
        Require(byDispatch == byScalar,
            "SIMD Subtract's AVX2 dispatch tier disagrees with the scalar reference at length " +
                std::to_string(length));

        byDispatch.assign(accumulator, accumulator + length);
        byScalar.assign(accumulator, accumulator + length);
        Nnue::Simd::Dispatch::AddSubtractAvx2(byDispatch.data(), added, removed, length);
        Nnue::Simd::Scalar::AddSubtract(byScalar.data(), added, removed, length);
        Require(byDispatch == byScalar,
            "SIMD AddSubtract's AVX2 dispatch tier disagrees with the scalar reference at "
            "length " + std::to_string(length));

        std::vector<Nnue::Weight> copyDispatch(length);
        std::vector<Nnue::Weight> copyScalar(length);
        Nnue::Simd::Dispatch::CopyAddAvx2(copyDispatch.data(), accumulator, added, length);
        Nnue::Simd::Scalar::CopyAdd(copyScalar.data(), accumulator, added, length);
        Require(copyDispatch == copyScalar,
            "SIMD CopyAdd's AVX2 dispatch tier disagrees with the scalar reference at length " +
                std::to_string(length));

        Nnue::Simd::Dispatch::CopySubtractAvx2(copyDispatch.data(), accumulator, added, length);
        Nnue::Simd::Scalar::CopySubtract(copyScalar.data(), accumulator, added, length);
        Require(copyDispatch == copyScalar,
            "SIMD CopySubtract's AVX2 dispatch tier disagrees with the scalar reference at "
            "length " + std::to_string(length));

        Nnue::Simd::Dispatch::CopyAddSubtractAvx2(copyDispatch.data(), accumulator, added,
            removed, length);
        Nnue::Simd::Scalar::CopyAddSubtract(copyScalar.data(), accumulator, added, removed,
            length);
        Require(copyDispatch == copyScalar,
            "SIMD CopyAddSubtract's AVX2 dispatch tier disagrees with the scalar reference at "
            "length " + std::to_string(length));
#else
        (void)accumulator;
        (void)added;
        (void)removed;
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

                const auto compareCopy = [&](std::string_view what,
                    void (*vector)(Nnue::Weight*, const Nnue::Weight*, const Nnue::Weight*, size_t),
                    void (*scalar)(Nnue::Weight*, const Nnue::Weight*, const Nnue::Weight*, size_t))
                {
                    std::vector<Nnue::Weight> byVector(length);
                    std::vector<Nnue::Weight> byScalar(length);
                    vector(byVector.data(), values.data(), added.data(), length);
                    scalar(byScalar.data(), values.data(), added.data(), length);
                    Require(byVector == byScalar,
                        "SIMD " + std::string(what) + " disagrees with the scalar reference at "
                        "length " + std::to_string(length));
                };
                compareCopy("CopyAdd", Nnue::Simd::CopyAdd, Nnue::Simd::Scalar::CopyAdd);
                compareCopy("CopySubtract", Nnue::Simd::CopySubtract,
                    Nnue::Simd::Scalar::CopySubtract);

                std::vector<Nnue::Weight> copyFusedVector(length);
                std::vector<Nnue::Weight> copyFusedScalar(length);
                Nnue::Simd::CopyAddSubtract(copyFusedVector.data(), values.data(), added.data(),
                    removed.data(), length);
                Nnue::Simd::Scalar::CopyAddSubtract(copyFusedScalar.data(), values.data(),
                    added.data(), removed.data(), length);
                Require(copyFusedVector == copyFusedScalar,
                    "SIMD CopyAddSubtract disagrees with the scalar reference at length " +
                        std::to_string(length));

                Require(Nnue::Simd::ActivatedDotProduct(values.data(), added.data(), length) ==
                    Nnue::Simd::Scalar::ActivatedDotProduct(values.data(), added.data(), length),
                    "SIMD ActivatedDotProduct disagrees with the scalar reference at length " +
                        std::to_string(length));

                RequireDispatchTiersAgree(values.data(), added.data(), length);
                RequireAccumulatorDispatchTierAgrees(values.data(), added.data(), removed.data(),
                    length);
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
        RequireAccumulatorDispatchTierAgrees(saturated.data(), extremeWeights.data(),
            extremeWeights.data(), saturated.size());
    }

    // Sorted active features for one perspective. Sorting is how two feature
    // sets are compared: the accumulator only sums weight columns, so the
    // order they arrive in is not part of the representation.
    std::vector<Nnue::FeatureIndex> SortedActiveFeatures(const ChessBoard& board,
        Nnue::Perspective perspective)
    {
        Nnue::FeatureSet::ActiveFeatures active;
        Nnue::FeatureSet::CollectActiveFeatures(board.GetBoardState(), perspective, active);
        std::vector<Nnue::FeatureIndex> indices(active.indices.begin(),
            active.indices.begin() + active.count);
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    void TestNnueFeatureIndexing()
    {
        using namespace NeraChessEngine;
        using Nnue::FeatureSet::Orientation;
        using Nnue::FeatureSet::View;

        // Indices must stay inside the input space the weight matrix covers,
        // whichever way round the perspective happens to be reading the board.
        for (uint8_t piece = 0; piece < 12; ++piece)
        {
            for (uint8_t square = 0; square < 64; ++square)
            {
                for (const Nnue::Perspective perspective :
                    { Nnue::Perspective::White, Nnue::Perspective::Black })
                {
                    for (const Orientation orientation :
                        { Orientation::Direct, Orientation::Mirrored })
                    {
                        for (uint8_t bucket = 0; bucket < Nnue::Architecture::InputBucketCount;
                            ++bucket)
                        {
                            const View view{ perspective, orientation, bucket };
                            const Nnue::FeatureIndex index =
                                Nnue::FeatureSet::FeatureIndexOf(view, Piece(piece), square);
                            Require(index < Nnue::Architecture::TotalInputSize,
                                "a feature index fell outside the input space");
                        }
                    }
                }
            }
        }

        // Every feature of every bucket must be reachable, and reachable only
        // once, or weights would go unused or be shared by two different
        // meanings. Each bucket owns a disjoint block of the input space and
        // between them they tile it: if two overlapped, training a position
        // with the king in one bucket would quietly corrupt another.
        std::set<Nnue::FeatureIndex> reached;
        for (uint8_t bucket = 0; bucket < Nnue::Architecture::InputBucketCount; ++bucket)
        {
            std::set<Nnue::FeatureIndex> withinBucket;
            for (uint8_t piece = 0; piece < 12; ++piece)
            {
                for (uint8_t square = 0; square < 64; ++square)
                {
                    withinBucket.insert(Nnue::FeatureSet::FeatureIndexOf(
                        View{ Nnue::Perspective::White, Orientation::Direct, bucket },
                        Piece(piece), square));
                }
            }
            Require(withinBucket.size() == Nnue::Architecture::PerspectiveInputSize,
                "the feature indexer does not cover one bucket exactly once");
            for (const Nnue::FeatureIndex index : withinBucket)
            {
                Require(reached.insert(index).second,
                    "two input buckets share a feature index");
            }
        }
        Require(reached.size() == Nnue::Architecture::TotalInputSize,
            "the input buckets do not tile the input space exactly once");

        // The two perspectives of one position must differ, otherwise the
        // network sees the same input for both sides.
        ChessBoard asymmetric("4k3/8/8/8/3P4/8/8/4K3 w - - 0 1");
        const std::vector<Nnue::FeatureIndex> whiteIndices =
            SortedActiveFeatures(asymmetric, Nnue::Perspective::White);
        const std::vector<Nnue::FeatureIndex> blackIndices =
            SortedActiveFeatures(asymmetric, Nnue::Perspective::Black);
        Require(whiteIndices.size() == 3 && blackIndices.size() == 3,
            "active feature count does not match the piece count");
        Require(whiteIndices != blackIndices,
            "both perspectives produced the same features for an asymmetric position");

        // A position and its colour-and-rank mirror must produce identical
        // features once each is read from its own side's perspective. This is
        // the property that lets both perspectives share one weight matrix.
        ChessBoard mirrored("4k3/8/8/3p4/8/8/8/4K3 b - - 0 1");
        Require(SortedActiveFeatures(mirrored, Nnue::Perspective::Black) == whiteIndices,
            "mirrored positions did not produce mirrored features");

        // A king move keeps its half valid only while it leaves both halves
        // of the view alone: the same horizontal orientation and the same
        // input bucket. e1-f1 changes neither.
        Require(!Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::White,
            Square::e1, Square::f1),
            "a king move within one view demanded a refresh");
        Require(Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::White,
            Square::e1, Square::d1),
            "a king move across the d/e boundary did not demand a refresh");
        Require(Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::Black,
            Square::d8, Square::e8),
            "a black king move across the d/e boundary did not demand a refresh");

        // Crossing a bucket boundary invalidates the half just as surely as
        // crossing the d/e one, even though the orientation is untouched:
        // e1 and h8 are both read direct, but sit in buckets 0 and 7.
        Require(Nnue::FeatureSet::ViewOfKing(Nnue::Perspective::White, Square::e1).orientation
                == Nnue::FeatureSet::ViewOfKing(
                       Nnue::Perspective::White, Square::h8).orientation,
            "e1 and h8 should be read with the same orientation");
        Require(Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::White,
            Square::e1, Square::h8),
            "a king move into another input bucket did not demand a refresh");
        Require(Nnue::FeatureSet::RequiresRefresh(Nnue::Perspective::Black,
            Square::e8, Square::h1),
            "a black king move into another input bucket did not demand a refresh");
    }

    // Horizontal canonicalization: each perspective reads the board so that
    // its own king always stands on files e..h, independently of the other.
    void TestNnueHorizontalMirroring()
    {
        using namespace NeraChessEngine;
        using Nnue::FeatureSet::Orientation;
        using Nnue::FeatureSet::View;
        using Nnue::Perspective;

        // -- the reflection itself: a<->h, b<->g, c<->f, d<->e -------------
        static constexpr uint8_t d4 = 27;
        static constexpr uint8_t e4 = 28;
        static constexpr uint8_t a5 = 32;
        static constexpr uint8_t h5 = 39;
        static constexpr std::pair<uint8_t, uint8_t> reflections[] = {
            { Square::a1, Square::h1 },
            { Square::b1, Square::g1 },
            { Square::c1, Square::f1 },
            { Square::d1, Square::e1 },
            { Square::a8, Square::h8 },
            { Square::d8, Square::e8 },
            { d4, e4 },
            { a5, h5 },
        };
        for (const auto& [left, right] : reflections)
        {
            Require(Nnue::FeatureSet::MirroredSquare(left) == right &&
                Nnue::FeatureSet::MirroredSquare(right) == left,
                "the horizontal reflection does not swap the expected files");
        }
        for (uint8_t square = 0; square < 64; ++square)
        {
            const uint8_t reflected = Nnue::FeatureSet::MirroredSquare(square);
            Require(Nnue::FeatureSet::MirroredSquare(reflected) == square,
                "the horizontal reflection is not its own inverse");
            Require(Square(reflected).GetRank() == Square(square).GetRank(),
                "the horizontal reflection moved a square off its rank");
            Require(Square(reflected).GetFile() == 7 - Square(square).GetFile(),
                "the horizontal reflection did not invert the file");
        }

        // -- which half of the board mirrors -------------------------------
        //
        // The convention: d mirrors, e does not, so a canonicalized king
        // always ends up on files e..h.
        Require(Nnue::FeatureSet::OrientationOfKing(Square::d1) == Orientation::Mirrored,
            "a king on the d file was not mirrored");
        Require(Nnue::FeatureSet::OrientationOfKing(Square::e1) == Orientation::Direct,
            "a king on the e file was mirrored");
        Require(Nnue::FeatureSet::OrientationOfKing(Square::a1) == Orientation::Mirrored,
            "a king on the a file was not mirrored");
        Require(Nnue::FeatureSet::OrientationOfKing(Square::h1) == Orientation::Direct,
            "a king on the h file was mirrored");
        for (uint8_t square = 0; square < 64; ++square)
        {
            const Orientation expected = Square(square).GetFile() <= 3
                ? Orientation::Mirrored
                : Orientation::Direct;
            Require(Nnue::FeatureSet::OrientationOfKing(square) == expected,
                "a king square fell on the wrong side of the d/e boundary");

            // Whatever the orientation, the canonicalized king lands on e..h.
            const View view = Nnue::FeatureSet::ViewOfKing(Perspective::White, square);
            Require(Square(Nnue::FeatureSet::OrientedSquare(view, square)).GetFile() >= 4,
                "canonicalization left a king on the queen side");
        }

        // -- the canonicalization is not a no-op ---------------------------
        const View direct{ Perspective::White, Orientation::Direct, 0 };
        const View reflected{ Perspective::White, Orientation::Mirrored, 0 };
        const Piece whiteRook{ PieceType::WHITE_ROOK };
        Require(Nnue::FeatureSet::FeatureIndexOf(direct, whiteRook, Square::a1) !=
            Nnue::FeatureSet::FeatureIndexOf(reflected, whiteRook, Square::a1),
            "a mirrored view numbered a1 the same way a direct one does");
        Require(Nnue::FeatureSet::FeatureIndexOf(reflected, whiteRook, Square::a1) ==
            Nnue::FeatureSet::FeatureIndexOf(direct, whiteRook, Square::h1),
            "a mirrored view did not read a1 where a direct view reads h1");

        // -- Black's mirroring composes with the flip it already applies ----
        const View blackReflected{ Perspective::Black, Orientation::Mirrored, 0 };
        const View blackDirect{ Perspective::Black, Orientation::Direct, 0 };
        const Piece blackRook{ PieceType::BLACK_ROOK };
        Require(Nnue::FeatureSet::FeatureIndexOf(blackDirect, blackRook, Square::a8) ==
            Nnue::FeatureSet::FeatureIndexOf(direct, whiteRook, Square::a1),
            "the vertical flip alone stopped agreeing between the perspectives");
        Require(Nnue::FeatureSet::FeatureIndexOf(blackReflected, blackRook, Square::a8) ==
            Nnue::FeatureSet::FeatureIndexOf(direct, whiteRook, Square::h1),
            "Black's mirroring does not compose with its vertical flip");

        // -- each perspective follows its own king -------------------------
        struct ViewCase
        {
            std::string_view name;
            std::string_view fen;
            Orientation white;
            Orientation black;
        };
        static constexpr ViewCase viewCases[] = {
            { "white queenside, black kingside", "7k/8/8/8/8/8/8/K7 w - - 0 1",
                Orientation::Mirrored, Orientation::Direct },
            { "white kingside, black queenside", "k7/8/8/8/8/8/8/7K w - - 0 1",
                Orientation::Direct, Orientation::Mirrored },
            { "both on the d file", "3k4/8/8/8/8/8/8/3K4 w - - 0 1",
                Orientation::Mirrored, Orientation::Mirrored },
            { "both on the e file", "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
                Orientation::Direct, Orientation::Direct },
        };
        for (const ViewCase& testCase : viewCases)
        {
            ChessBoard board{ std::string(testCase.fen) };
            Require(Nnue::FeatureSet::ViewOf(board.GetBoardState(), Perspective::White)
                    .orientation == testCase.white,
                "White's orientation is wrong in " + std::string(testCase.name));
            Require(Nnue::FeatureSet::ViewOf(board.GetBoardState(), Perspective::Black)
                    .orientation == testCase.black,
                "Black's orientation is wrong in " + std::string(testCase.name));
        }

        // -- a position and its complete horizontal reflection --------------
        //
        // This is the invariant the whole feature set exists for: reflecting
        // every piece, both kings included, must leave the canonical features
        // untouched from both points of view. Positions carry no castling
        // rights and no en passant square, so the two differ in nothing but
        // the file every piece stands on.
        struct ReflectionCase
        {
            std::string_view name;
            std::string_view fen;
            std::string_view reflectedFen;
        };
        static constexpr ReflectionCase reflectionCases[] = {
            { "castled pawn shelters",
                "2k5/1pp5/8/8/8/8/1PP5/2K5 w - - 0 1",
                "5k2/5pp1/8/8/8/8/5PP1/5K2 w - - 0 1" },
            { "kings across the boundary",
                "8/3k4/2p5/8/8/5N2/6PP/6K1 w - - 0 1",
                "8/4k3/5p2/8/8/2N5/PP6/1K6 w - - 0 1" },
            { "queen against a bare king",
                "7k/8/8/3q4/8/8/8/1K6 w - - 0 1",
                "k7/8/8/4q3/8/8/8/6K1 w - - 0 1" },
            { "black to move",
                "1r2k3/p1pp4/8/8/8/6N1/PP4PP/2K4R b - - 0 1",
                "3k2r1/4pp1p/8/8/8/1N6/PP4PP/R4K2 b - - 0 1" },
        };
        for (const ReflectionCase& testCase : reflectionCases)
        {
            ChessBoard board{ std::string(testCase.fen) };
            ChessBoard reflectedBoard{ std::string(testCase.reflectedFen) };
            Require(board.GetBoardState().pieceBitboards !=
                reflectedBoard.GetBoardState().pieceBitboards,
                "reflection case " + std::string(testCase.name) +
                    " is its own reflection, so it proves nothing");

            for (const Perspective perspective : { Perspective::White, Perspective::Black })
            {
                Require(SortedActiveFeatures(board, perspective) ==
                    SortedActiveFeatures(reflectedBoard, perspective),
                    "reflection case " + std::string(testCase.name) +
                        " did not canonicalize to the same features");
            }
        }
    }

    // King buckets: the canonical square of a perspective's own king chooses
    // which of the InputBucketCount feature-transformer matrices that
    // perspective's features index. The bucket is read off the *canonical*
    // square, so it composes with the mirroring above rather than undoing it.
    void TestNnueKingBuckets()
    {
        using namespace NeraChessEngine;
        using Nnue::FeatureSet::Orientation;
        using Nnue::Perspective;

        // -- the map stays inside the matrices that exist ------------------
        for (const Perspective perspective : { Perspective::White, Perspective::Black })
        {
            for (uint8_t square = 0; square < 64; ++square)
            {
                const Nnue::FeatureSet::View view =
                    Nnue::FeatureSet::ViewOfKing(perspective, square);
                Require(view.inputBucket < Nnue::Architecture::InputBucketCount,
                    "a king square selected an input bucket that does not exist");
            }
        }

        // -- the map only ever sees canonical squares ----------------------
        // Mirroring puts the own king on files e..h, so the table's domain is
        // 32 squares. Anything else reaching it would index the wrong row.
        for (const Perspective perspective : { Perspective::White, Perspective::Black })
        {
            for (uint8_t square = 0; square < 64; ++square)
            {
                Require((Nnue::FeatureSet::CanonicalKingSquare(perspective, square) % 8u) >= 4u,
                    "a canonical king square fell outside files e..h");
            }
        }

        // -- reflected king squares share a bucket -------------------------
        // This is the property that makes buckets and mirroring compose. A
        // bucket read off the raw square instead of the canonical one would
        // split a position and its reflection apart, undoing the mirroring
        // the feature set exists to exploit.
        for (const Perspective perspective : { Perspective::White, Perspective::Black })
        {
            for (uint8_t square = 0; square < 64; ++square)
            {
                Require(Nnue::FeatureSet::ViewOfKing(perspective, square).inputBucket ==
                    Nnue::FeatureSet::ViewOfKing(
                        perspective, Nnue::FeatureSet::MirroredSquare(square)).inputBucket,
                    "a king square and its horizontal reflection chose different buckets");
            }
        }

        // -- the two perspectives agree about their own king ---------------
        // White's king on e1 and Black's on e8 stand in the same place
        // relative to their own side, so they must select the same matrix.
        for (uint8_t square = 0; square < 64; ++square)
        {
            Require(Nnue::FeatureSet::ViewOfKing(Perspective::White, square).inputBucket ==
                Nnue::FeatureSet::ViewOfKing(Perspective::Black,
                    Nnue::FeatureSet::RelativeSquare(Perspective::Black, square)).inputBucket,
                "the perspectives disagree about where their own king stands");
        }

        // -- every bucket is reachable from some king square ---------------
        // A bucket no king square selects would be weights nothing can reach.
        std::set<uint8_t> reachable;
        for (uint8_t square = 0; square < 64; ++square)
            reachable.insert(Nnue::FeatureSet::ViewOfKing(Perspective::White, square).inputBucket);
        Require(reachable.size() == Nnue::Architecture::InputBucketCount,
            "some input bucket is unreachable from every king square");

        // -- the known layout ----------------------------------------------
        // Pins the actual map, so re-tuning it is a deliberate edit here and
        // in features.py rather than a silent change of what a network means.
        struct BucketCase
        {
            std::string_view name;
            uint8_t square;
            uint8_t bucket;
        };
        // Square only names ranks 1 and 8, so the rest are ordinals: a1 is 0
        // and squares run a..h within a rank, so a square is rank * 8 + file.
        static constexpr BucketCase layout[] = {
            { "e1", Square::e1, 0 }, { "f1", Square::f1, 0 },
            { "g1", Square::g1, 1 }, { "h1", Square::h1, 1 },
            { "e2", 12, 2 }, { "g2", 14, 3 },
            { "e4", 28, 4 }, { "g4", 30, 5 },
            { "e6", 44, 6 }, { "h7", 55, 7 },
        };
        for (const BucketCase& testCase : layout)
        {
            Require(Nnue::FeatureSet::ViewOfKing(Perspective::White, testCase.square)
                        .inputBucket == testCase.bucket,
                "king bucket layout changed at " + std::string(testCase.name));
        }

        // -- a position with no king reads bucket zero ---------------------
        // Malformed, but ViewOf and the Python indexer must not disagree
        // about anything, including the cases no legal game reaches.
        {
            ChessBoard kingless("8/8/8/3p4/8/8/8/8 w - - 0 1");
            Require(Nnue::FeatureSet::KingSquare(
                        kingless.GetBoardState(), Perspective::White) == Nnue::NoSquare,
                "the kingless position was read as holding a white king");
            const Nnue::FeatureSet::View view =
                Nnue::FeatureSet::ViewOf(kingless.GetBoardState(), Perspective::White);
            Require(view.inputBucket == 0 && view.orientation == Orientation::Direct,
                "a position without a king did not read bucket 0 and Direct");
        }

        // -- moving the king between buckets renumbers quiet pieces --------
        // The rook does not move, and still changes index. That is exactly
        // why the accumulator must refresh that half rather than take a
        // delta, and it is the whole reason buckets buy anything: the same
        // rook on the same square means something different to a network
        // when the king it defends stands somewhere else.
        {
            ChessBoard onFirstRank("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
            ChessBoard onSecondRank("4k3/8/8/8/8/8/4K3/R7 w - - 0 1");
            Require(Nnue::FeatureSet::ViewOf(onFirstRank.GetBoardState(), Perspective::White)
                        .inputBucket !=
                    Nnue::FeatureSet::ViewOf(onSecondRank.GetBoardState(), Perspective::White)
                        .inputBucket,
                "the two king placements were expected to sit in different buckets");
            Require(SortedActiveFeatures(onFirstRank, Perspective::White) !=
                SortedActiveFeatures(onSecondRank, Perspective::White),
                "a king bucket change left the other pieces' features untouched");
        }
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
        // Every case here must leave both perspectives' views alone, which is
        // the precondition a plain delta has. That is a narrower requirement
        // than it once was: a view now changes on a king bucket boundary as
        // well as on the d/e one, so the castles and the c1-b1 king step that
        // used to sit in this list have moved to TestNnueOrientationRefresh.
        // Both horizontal orientations appear, so the delta path is covered
        // mirrored as well as direct.
        static constexpr DeltaCase cases[] = {
            { "quiet", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4" },
            { "capture", "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5" },
            { "en passant", "8/8/1k6/8/3pP3/8/6K1/8 b - e3 0 1", "d4e3" },
            { "promotion", "7k/P7/8/8/8/8/8/K7 w - - 0 1", "a7a8q" },
            { "capture promotion", "1r5k/P7/8/8/8/8/8/K7 w - - 0 1", "a7b8q" },
            // King steps that stay inside their bucket, so the delta still
            // applies even though the king itself is one of the dirty pieces.
            // e1-f1 shares bucket 0 and g1-h1 shares bucket 1.
            { "king step within a bucket", "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1f1" },
            { "king step within a castled bucket", "4k3/8/8/8/8/8/8/6K1 w - - 0 1",
                "g1h1" },
            // Both kings on the queen side, so both halves read the board
            // mirrored while the pieces move.
            { "mirrored quiet", "2k5/1pp5/8/8/8/8/1PP5/2K5 w - - 0 1", "b2b4" },
            { "mirrored capture", "2k5/1p6/8/8/8/2n5/1P6/2K5 w - - 0 1", "b2c3" },
            { "mirrored king step", "3k4/1pp5/8/8/8/8/1PP5/3K4 w - - 0 1", "d1c1" },
            { "mirrored promotion", "1k6/2P5/8/8/8/8/8/2K5 w - - 0 1", "c7c8q" },
            // One half mirrored and the other not, which is the case a shared
            // orientation would silently get wrong.
            { "split orientation", "r6k/1p6/8/8/3K4/8/6P1/R7 w - - 0 1", "g2g4" },
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
            ChessBoard after = board;
            after.MakeMove(move);

            for (const Nnue::Perspective perspective :
                { Nnue::Perspective::White, Nnue::Perspective::Black })
            {
                const Nnue::FeatureSet::View view =
                    Nnue::FeatureSet::ViewOf(board.GetBoardState(), perspective);
                Require(view == Nnue::FeatureSet::ViewOf(after.GetBoardState(), perspective),
                    "NNUE delta case " + std::string(testCase.name) +
                        " changes a perspective's view, so a plain delta cannot apply");

                Nnue::FeatureSet::FeatureDelta delta;
                Nnue::FeatureSet::ComputeDelta(dirty, view, delta);
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

    // A king crossing the d/e boundary renumbers every feature of its own
    // half, so that half cannot be brought forward with a delta. The other
    // half's own king has not moved, so its view still stands and it takes the
    // ordinary delta -- it simply sees an enemy king change square.
    //
    // This is the case an incremental accumulator gets wrong by default, and
    // it gets it wrong quietly: the values stay plausible and the engine just
    // evaluates some positions as though the pieces stood elsewhere.
    void TestNnueOrientationRefresh()
    {
        using namespace NeraChessEngine;

        const std::vector<std::byte> file = BuildSyntheticNetwork(0x9E3779B97F4A7C15ull);
        Nnue::Network network;
        Require(network.LoadFromMemory(file) == Nnue::NetworkFormat::Status::Ok,
            "the synthetic network failed to load for the orientation tests");

        struct CrossingCase
        {
            std::string_view name;
            std::string_view fen;
            std::string_view move;
            bool whiteCrosses;
            bool blackCrosses;
        };
        static constexpr CrossingCase cases[] = {
            { "Kd4-e4", "r6k/1p6/8/8/3K4/8/6P1/R7 w - - 0 1", "d4e4", true, false },
            { "Ke4-d4", "r6k/1p6/8/8/4K3/8/6P1/R7 w - - 0 1", "e4d4", true, false },
            { "Kd6-e6", "r7/1p6/3k4/8/8/8/6P1/R6K b - - 0 1", "d6e6", false, true },
            { "Ke6-d6", "r7/1p6/4k3/8/8/8/6P1/R6K b - - 0 1", "e6d6", false, true },
            { "white queenside castle", "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1", true, false },
            { "black queenside castle", "r3k3/8/8/8/8/8/8/4K3 b q - 0 1", "e8c8", false, true },
            // Crossing a king bucket boundary invalidates a half exactly as
            // the d/e boundary does, but for a different reason: the
            // orientation is untouched and the whole matrix changes instead.
            // A half that noticed only the orientation would take a delta
            // here and read every one of its features out of the wrong
            // weights, which is the same quiet wrongness in a new place.
            { "white kingside castle", "4k3/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1", true, false },
            { "black kingside castle", "4k2r/8/8/8/8/8/8/4K3 b k - 0 1", "e8g8", false, true },
            { "Ke1-e2", "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1e2", true, false },
            { "Kg1-g2", "4k3/8/8/8/8/8/8/6K1 w - - 0 1", "g1g2", true, false },
            { "Ke4-e5", "r6k/1p6/8/8/4K3/8/6P1/R7 w - - 0 1", "e4e5", true, false },
            // Mirrored, so the bucket changes while the half already reads
            // the board reflected -- the two mechanisms have to compose.
            { "mirrored Kc1-b1", "2k5/1pp5/8/8/8/8/1PP5/2K5 w - - 0 1", "c1b1", true, false },
            // The king moves without changing orientation or bucket, so
            // neither half is renumbered and both stay incremental.
            { "Ke4-f4", "r6k/1p6/8/8/4K3/8/6P1/R7 w - - 0 1", "e4f4", false, false },
            { "Kc4-d4", "r6k/1p6/8/8/2K5/8/6P1/R7 w - - 0 1", "c4d4", false, false },
        };

        for (const CrossingCase& testCase : cases)
        {
            ChessBoard board{ std::string(testCase.fen) };
            const Move move = FindMove(board, testCase.move);
            Require(move != 0,
                "crossing case " + std::string(testCase.name) + " has no such legal move");

            Nnue::AccumulatorStack stack;
            stack.Reset(network, board.GetBoardState());
            const Nnue::Accumulator parent = stack.Current();
            Require(parent.computed, "the root accumulator was left stale");

            const Nnue::DirtyPieces dirty = Nnue::DescribeMove(board, move);
            board.MakeMove(move);
            stack.Push(network, board.GetBoardState(), dirty);

            // The views the position implies must be exactly the ones the
            // orientation bookkeeping predicted, and only the crossing side's
            // may have moved.
            for (const auto& [perspective, crosses] : {
                std::pair<Nnue::Perspective, bool>{ Nnue::Perspective::White,
                    testCase.whiteCrosses },
                std::pair<Nnue::Perspective, bool>{ Nnue::Perspective::Black,
                    testCase.blackCrosses } })
            {
                const Nnue::FeatureSet::View after =
                    Nnue::FeatureSet::ViewOf(board.GetBoardState(), perspective);
                const bool changed = after != parent.views[Nnue::Index(perspective)];
                Require(changed == crosses,
                    "crossing case " + std::string(testCase.name) +
                        " changed the wrong perspective's view");
                Require(stack.Current().views[Nnue::Index(perspective)] == after,
                    "crossing case " + std::string(testCase.name) +
                        " left a half tagged with a view the position does not imply");
            }

            // The point of the whole exercise: whichever path Push took, the
            // result has to be the accumulator the resulting position has.
            Nnue::Accumulator refreshed;
            refreshed.Refresh(network, board.GetBoardState());
            Require(stack.Current().computed,
                "crossing case " + std::string(testCase.name) + " left the child stale");
            Require(stack.Current().values == refreshed.values,
                "crossing case " + std::string(testCase.name) +
                    " diverged from a full refresh of the resulting position");

            // Undoing must hand the parent back untouched, which is what lets
            // the search unmake a move without recomputing anything.
            board.UndoMove(move);
            stack.Pop();
            Require(stack.Current().values == parent.values &&
                stack.Current().views == parent.views &&
                stack.Current().computed,
                "crossing case " + std::string(testCase.name) +
                    " disturbed the parent accumulator");
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
            // Both kings walk back and forth across the d/e boundary, so the
            // orientation refresh runs inside a real move tree rather than
            // only in the positions a test picked for it.
            { "kings crossing", "8/4k3/8/8/8/8/3K4/8 w - - 0 1", 4 },
            { "queenside castling", "r3k3/1p4p1/8/8/8/8/1P4P1/R3K3 w Qq - 0 1", 3 },
            // The same, for king buckets. Kings on their own second rank can
            // step to the first, the third or along it, which crosses three
            // bucket boundaries in the branching part of the tree -- and the
            // walk undoes every one of them, so Pop has to hand back a parent
            // that was never written through.
            { "kings changing bucket", "8/8/8/8/8/8/3KP1k1/8 w - - 0 1", 4 },
            // Castling both ways from one position: e1-g1 and e1-c1 leave the
            // king in different buckets, and only one of them also flips the
            // orientation, so the two reasons for a refresh appear in the
            // same tree and can be told apart if only one is implemented.
            { "castling changes bucket", "r3k2r/1p4p1/8/8/8/8/1P4P1/R3K2R w KQkq - 0 1", 3 },
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

        // In-tree twofold repetition (issue #18). White is down a queen and two
        // rooks with nothing but a perpetual check (Qe6+/Qh3+) to save the game.
        // The game-rule draw test needs a genuine threefold -- the cycle walked
        // twice -- to see this, so at a depth deep enough to find the perpetual
        // but not yet a third occurrence, it still reported a large score for the
        // side about to be perpetually checked. This fails on that older
        // behavior (score 51) and passes once a position repeating once since the
        // root is scored a draw immediately.
        search.NewGame();
        ChessBoard perpetual("rr3bk1/6p1/8/8/q7/3B4/4Q3/6K1 w - - 0 1");
        SearchLimits perpetualLimits;
        perpetualLimits.maxDepth = 12;
        const SearchResult perpetualResult = search.Search(perpetual, perpetualLimits);
        Require(perpetualResult.score == SCORE_DRAW,
            "in-tree repetition of a forced perpetual was not scored as a draw");

        // Same defect, conversion phase: a won Q+N vs R+R endgame whose reported
        // principal variation cycles back to a position it has already passed
        // through, which a correct search should never prefer over progress.
        search.NewGame();
        ChessBoard endgame("8/8/2n5/1k6/1p2q3/8/1R5K/1R6 b - - 7 55");
        SearchLimits endgameLimits;
        endgameLimits.maxDepth = 13;
        const SearchResult endgameResult = search.Search(endgame, endgameLimits);
        std::vector<uint64_t> pvKeys{ endgame.GetZobristKey() };
        ChessBoard pvWalker = endgame;
        for (const NeraChessEngine::Move pvMove : endgameResult.principalVariation)
        {
            pvWalker.MakeMove(pvMove, true);
            const uint64_t key = pvWalker.GetZobristKey();
            Require(std::find(pvKeys.begin(), pvKeys.end(), key) == pvKeys.end(),
                "principal variation of a won endgame repeated a position it had already passed through");
            pvKeys.push_back(key);
        }
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
        // Both horizontal orientations, in both perspectives and in both
        // combinations, so the fixture pins down the mirroring rather than
        // only the layout underneath it.
        static constexpr std::string_view fens[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1",
            "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            // Both kings on the queen side: both halves read mirrored.
            "2kr3r/pp3ppp/8/8/8/8/PPP2PPP/2KR3R w - - 0 1",
            // White mirrored, Black direct.
            "r6k/1p6/8/8/3K4/8/6P1/R7 w - - 0 1",
            // White direct, Black mirrored, and Black to move.
            "3k2r1/4pp1p/8/8/8/1N6/PP4PP/R4K2 b - - 0 1",
            // The eight positions above only reach input buckets 0, 1, 4 and
            // 7. These five carry the kings into the remaining four, in both
            // orientations, so the fixture pins the bucket map and not only
            // the square layout underneath it.
            "4k3/8/8/8/8/8/4K3/8 w - - 0 1",     // White bucket 2, Black 0
            "8/7k/8/8/8/8/6K1/8 w - - 0 1",      // both bucket 3, both direct
            "8/8/8/1k6/6K1/8/8/8 w - - 0 1",     // both bucket 5, one mirrored
            "8/8/8/8/8/3k4/8/1K6 w - - 0 1",     // White 1, Black 6, both mirrored
            "8/7K/8/8/8/8/k7/8 w - - 0 1",       // both bucket 7, one mirrored
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
        Require(defaultLimits.hardTime > seconds{ 15 } && defaultLimits.hardTime < minutes{ 1 },
            "default time manager hard limit is incorrect");

        // The budget must scale with the clock instead of pinning to a fixed
        // ceiling once the per-move share crosses it (issue: time management
        // caps every search at 10s soft / 15s hard regardless of time control).
        Clock longClock(minutes{ 90 }, seconds{ 40 });
        const auto longLimits = CalculateLimits(board, longClock);
        Require(longLimits.softTime > defaultLimits.softTime * 5,
            "time manager budget did not scale up for a much longer clock");
        Require(longLimits.hardTime > defaultLimits.hardTime * 5,
            "time manager hard limit did not scale up for a much longer clock");
        Require(longLimits.hardTime < minutes{ 90 },
            "time manager hard limit ignored the clock safety reserve");

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
        TestFastInCheck();
        TestNullMoveState();
        TestTranspositionTable();
        TestConcurrentTranspositionTable();
        TestSearchFoundations();
        TestFiftyMoveTranspositions();
        TestMultithreadedSearch();
        TestNnueSimdKernels();
        TestNnueNetworkFormat();
        TestNnueFeatureIndexing();
        TestNnueHorizontalMirroring();
        TestNnueKingBuckets();
        TestNnueAccumulatorUpdates();
        TestNnueOrientationRefresh();
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
