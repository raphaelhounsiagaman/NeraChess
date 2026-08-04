#include "NeraChessBot.h"

#include "Resources.h"

#include <chrono>
#include <iostream>
#include <string>

NeraChessBot::NeraChessBot()
    : m_OpeningBookPath(NeraChessApp::GetResourcePath("OpeningBook/OpeningBook.txt")),
      m_OpeningBook(m_OpeningBookPath)
{
    if (!m_OpeningBook)
    {
        std::cout << "Opening book missing (" << m_OpeningBookPath << ")\n";
        m_OpeningBookAvailable = false;
    }
}

NeraChessEngine::Move NeraChessBot::GetNextMove(const NeraChessEngine::ChessBoard& board,
    const NeraChessEngine::Clock& timer)
{
    (void)timer;
    if (m_OpeningBookAvailable)
    {
        const NeraChessEngine::Move bookMove = GetOpeningBookMove(board);
        if (bookMove != 0)
            return bookMove;
        m_OpeningBookAvailable = false;
    }

    NeraChessSearch::SearchLimits limits;
    limits.maxDepth = NeraChessSearch::MAX_PLY - 1;
    limits.softTime = std::chrono::milliseconds(14'500);
    limits.hardTime = std::chrono::milliseconds(15'000);
    const NeraChessSearch::SearchResult result = m_SearchEngine.Search(board, limits);

    std::cout << "Search depth " << result.completedDepth << ", score " << result.score
              << ", nodes " << result.nodes << ", time " << result.elapsed.count() << " ms\n";
    return result.bestMove;
}

void NeraChessBot::ResetGame()
{
    m_OpeningBookAvailable = m_OpeningBook.is_open();
    m_SearchEngine.NewGame();
}

void NeraChessBot::StopSearching()
{
    m_SearchEngine.RequestStop();
}

NeraChessEngine::Move NeraChessBot::GetOpeningBookMove(const NeraChessEngine::ChessBoard& board)
{
    const std::string fen = board.GetFENString();
    std::string uciMove;
    std::string line;
    while (std::getline(m_OpeningBook, line))
    {
        const size_t separator = line.find(',');
        if (separator == std::string::npos)
            continue;
        if (line.compare(0, separator, fen) == 0)
        {
            uciMove = line.substr(separator + 1);
            break;
        }
    }

    m_OpeningBook.clear();
    m_OpeningBook.seekg(0);
    if (uciMove.empty())
        return 0;

    const NeraChessEngine::Move parsedMove(uciMove);
    for (const NeraChessEngine::Move move : board.GetLegalMoves())
    {
        if (move.GetStartSquare() == parsedMove.GetStartSquare() &&
            move.GetTargetSquare() == parsedMove.GetTargetSquare() &&
            (uciMove.size() != 5 || move.ToUCI() == uciMove))
            return move;
    }
    return 0;
}
