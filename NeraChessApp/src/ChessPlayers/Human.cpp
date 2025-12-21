#include "Human.h"

#include "Core/Application.h"

#include "BoardLayer.h"

#include <thread>
#include <chrono>

ChessCore::Move Human::GetNextMove(const ChessCore::ChessBoard& board, const ChessCore::Clock& timer)
{
    NeraCore::Application& app = NeraCore::Application::Get();

    BoardLayer* boardLayer = app.GetLayer<BoardLayer>();

    ChessCore::Move move = 0;

    boardLayer->SetMovePtr(&move);

    while (!m_StopSearching)
    {
        if (move)
            return move;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    m_StopSearching = false;

    return 0;
}
