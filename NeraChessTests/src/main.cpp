#include "ChessBoard.h"
#include "Clock.h"
#include "MoveOrdering.h"
#include "OpeningBook.h"
#include "SearchEngine.h"
#include "TimeManagement.h"
#include "TranspositionTable.h"
#include "Zobrist.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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

    void TestNullMoveState()
    {
        ChessBoard board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 17 42");
        const ChessBoard original = board;
        const std::string originalFen = board.GetFENString();
        const uint64_t originalKey = board.GetZobristKey();

        Require(board.MakeNullMove(), "legal null move was rejected");
        Require(!board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove),
            "null move did not toggle side to move");
        Require(!board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::CanEnPassent),
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
        Require(table.Probe(0) == nullptr, "empty TT reported a key-zero hit");

        table.NewSearch();
        table.Store(0, 42, 8, TTBound::Exact, NeraChessEngine::Move(123));
        const TTEntry* zeroEntry = table.Probe(0);
        Require(zeroEntry && zeroEntry->score == 42 && zeroEntry->depth == 8,
            "TT failed to store key zero");

        constexpr uint64_t key = 0x123456789ABCDEF0ULL;
        table.Store(key, 300, 12, TTBound::Exact, NeraChessEngine::Move(456));
        table.Store(key, -50, 2, TTBound::Upper, NeraChessEngine::Move(789));
        const TTEntry* preserved = table.Probe(key);
        Require(preserved && preserved->score == 300 && preserved->depth == 12 &&
            preserved->GetBound() == TTBound::Exact,
            "shallow TT bound replaced a deep exact entry");
        Require(preserved->move == 789, "TT did not refresh the best move");

        table.Clear();
        Require(table.Probe(key) == nullptr, "TT clear left a valid entry");
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
        abortLimits.maxDepth = 12;
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

        SearchEngine search(16);
        SearchLimits limits;
        limits.maxDepth = 3;
        ChessBoard ordinary("7k/8/8/8/8/3Q4/8/K7 w - - 0 1");
        Require(search.Search(ordinary, limits).score > 800,
            "winning endgame was not evaluated as winning");

        ChessBoard nearDraw("7k/8/8/8/8/3Q4/8/K7 w - - 98 1");
        const SearchResult draw = search.Search(nearDraw, limits);
        Require(draw.score == SCORE_DRAW,
            "transposition score bypassed the approaching 50-move draw");
    }

    void TestTaperedEvaluation()
    {
        using NeraChessSearch::SearchEngine;

        ChessBoard initialWhite;
        ChessBoard initialBlack("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
        Require(SearchEngine::Evaluate(initialWhite) == 10,
            "symmetric initial position did not receive only the tempo bonus");
        Require(SearchEngine::Evaluate(initialBlack) == 10,
            "evaluation is not symmetric with black to move");

        ChessBoard whiteQueen("4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1");
        ChessBoard whiteQueenBlackTurn("4k3/8/8/8/3Q4/8/8/4K3 b - - 0 1");
        Require(SearchEngine::Evaluate(whiteQueen) > 800,
            "material advantage is missing from tapered evaluation");
        Require(SearchEngine::Evaluate(whiteQueen) + SearchEngine::Evaluate(whiteQueenBlackTurn) == 20,
            "side-to-move evaluation is not antisymmetric apart from tempo");

        ChessBoard rimKnight("4k3/8/8/8/8/8/8/N3K3 w - - 0 1");
        ChessBoard centralKnight("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1");
        Require(SearchEngine::Evaluate(centralKnight) > SearchEngine::Evaluate(rimKnight) + 40,
            "piece-square evaluation does not reward a centralized knight");

        ChessBoard backPawn("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
        ChessBoard advancedPawn("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        Require(SearchEngine::Evaluate(advancedPawn) > SearchEngine::Evaluate(backPawn) + 100,
            "endgame evaluation does not reward an advanced pawn");

        ChessBoard structuredWhite("4k3/8/8/8/3P4/2P5/8/2B1KB1R w - - 0 1");
        ChessBoard structuredBlack("2b1kb1r/8/2p5/3p4/8/8/8/4K3 b - - 0 1");
        Require(SearchEngine::Evaluate(structuredWhite) == SearchEngine::Evaluate(structuredBlack),
            "positional evaluation is not color-and-rank symmetric");
    }

    void TestSearchChoices()
    {
        using namespace NeraChessSearch;

        SearchEngine search(32);
        SearchLimits limits;
        limits.maxDepth = 6;

        ChessBoard strategic("r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 9");
        Require(search.Search(strategic, limits).bestMove == FindMove(strategic, "d4c5"),
            "search missed the benchmark's strongest strategic break");

        search.NewGame();
        ChessBoard tactical("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        const auto bestMove = search.Search(tactical, limits).bestMove;
        Require(bestMove == FindMove(tactical, "d5e6") || bestMove == FindMove(tactical, "e2a6"),
            "search missed both top tactical continuations in the benchmark");
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
            "NeraChessApp/Ressources/OpeningBook/OpeningBook.txt");
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

        TestFenValidation();
        TestPerft();
        TestMakeUndoInvariants();
        TestTerminalPositions();
        TestRepetition();
        TestIncrementalZobrist();
        TestNullMoveState();
        TestTranspositionTable();
        TestSearchFoundations();
        TestFiftyMoveTranspositions();
        TestTaperedEvaluation();
        TestSearchChoices();
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
