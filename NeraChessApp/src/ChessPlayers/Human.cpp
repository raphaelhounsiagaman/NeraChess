#include "Human.h"

#include "Core/Application.h"

#include "BoardLayer.h"

#include <thread>
#include <chrono>

NeraChessEngine::Move Human::GetNextMove(const NeraChessEngine::ChessBoard& board, const NeraChessEngine::Clock& timer)
{
    ApplicationCore::Application& app = ApplicationCore::Application::Get();

    BoardLayer* boardLayer = app.GetLayer<BoardLayer>();
    if (!boardLayer)
        return 0;

    NeraChessEngine::Move move = 0;

    boardLayer->BeginHumanMoveInput();

    while (!m_StopSearching)
    {
        if (boardLayer->TryGetHumanMove(&move))
            return move;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    boardLayer->CancelHumanMoveInput();
    m_StopSearching = false;

    return 0;
}
