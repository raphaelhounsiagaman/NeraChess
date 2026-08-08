#include "Human.h"

#include "BoardLayer.h"
#include "Core/Application.h"

#include <chrono>
#include <thread>

NeraChessEngine::Move Human::GetNextMove(const NeraChessEngine::ChessBoard& board, const NeraChessEngine::Clock& timer)
{
	ApplicationCore::Application& app = ApplicationCore::Application::Get();

	BoardLayer* boardLayer = app.GetLayer<BoardLayer>();
	if (!boardLayer)
		return 0;

	NeraChessEngine::Move move = 0;

	boardLayer->BeginHumanMoveInput();

	const bool whiteToMove = board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove);

	while (!m_StopSearching)
	{
		if (boardLayer->TryGetHumanMove(move))
			return move;
		if (timer.HasExpired(whiteToMove))
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	boardLayer->CancelHumanMoveInput();
	m_StopSearching = false;

	return 0;
}
