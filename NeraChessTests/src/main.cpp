#include "ChessBoard.h"

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

        TestPerft();
        TestMakeUndoInvariants();
        TestTerminalPositions();
        std::cout << "All NeraChess engine tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
