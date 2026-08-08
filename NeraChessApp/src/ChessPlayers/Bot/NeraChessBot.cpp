#include "NeraChessBot.h"

#include "Resources.h"
#include "TimeManagement.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>

NeraChessBot::NeraChessBot()
    : m_OpeningBookPath(NeraChessApp::GetResourcePath("OpeningBook/OpeningBook.txt")),
      m_OpeningBook(m_OpeningBookPath)
{
    const unsigned hardwareThreads = std::max(1U, std::thread::hardware_concurrency());
    m_SearchEngine.SetThreadCount(std::min<size_t>(4, hardwareThreads));
    if (!m_OpeningBook.IsAvailable())
        std::cout << "Opening book missing (" << m_OpeningBookPath << ")\n";
}

NeraChessEngine::Move NeraChessBot::GetNextMove(const NeraChessEngine::ChessBoard& board,
    const NeraChessEngine::Clock& timer)
{
    m_SearchEngine.PrepareSearch();
    const NeraChessEngine::Move bookMove = m_OpeningBook.FindMove(board);
    if (bookMove != 0)
        return bookMove;

    const NeraChessSearch::SearchLimits limits =
        NeraChessSearch::TimeManagement::CalculateLimits(board, timer);
    const NeraChessSearch::SearchResult result = m_SearchEngine.Search(board, limits);

    std::cout << "Search depth " << result.completedDepth << ", score " << result.score
              << ", nodes " << result.nodes << ", time " << result.elapsed.count() << " ms\n";
    return result.bestMove;
}

void NeraChessBot::ResetGame()
{
    m_SearchEngine.NewGame();
}

void NeraChessBot::StopSearching()
{
    m_SearchEngine.RequestStop();
}
