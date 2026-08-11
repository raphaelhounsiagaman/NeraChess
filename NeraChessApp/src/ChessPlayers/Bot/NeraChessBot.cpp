#include "NeraChessBot.h"

#include "Evaluation.h"
#include "Resources.h"
#include "TimeManagement.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>

NeraChessBot::NeraChessBot()
    : m_OpeningBookPath(NeraChessApp::GetResourcePath("OpeningBook/OpeningBook.txt")),
      m_NetworkPath(NeraChessApp::GetResourcePath("NNUE/nera.nnue")),
      m_OpeningBook(m_OpeningBookPath)
{
    const unsigned hardwareThreads = std::max(1U, std::thread::hardware_concurrency());
    m_SearchEngine.SetThreadCount(std::min<size_t>(4, hardwareThreads));
    if (!m_OpeningBook.IsAvailable())
        std::cout << "Opening book missing (" << m_OpeningBookPath << ")\n";

    // The network is process-wide, and this runs while the application is
    // still assembling its layers, so no search can be reading it yet.
    std::string message;
    if (NeraChessSearch::Evaluation::LoadNetwork(m_NetworkPath, message))
    {
        std::cout << message << '\n';
    }
    else
    {
        // A source build has no bundled network; try one sitting beside the
        // executable before giving up.
        NeraChessSearch::Evaluation::LoadDefaultNetwork({});
        std::cout << NeraChessSearch::Evaluation::StatusText() << '\n';
        if (!NeraChessSearch::Evaluation::IsNetworkLoaded())
        {
            std::cout << "No NNUE network found; the bot searches without an evaluation "
                      << "and will play badly. Place one at " << m_NetworkPath << ".\n";
        }
    }
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
