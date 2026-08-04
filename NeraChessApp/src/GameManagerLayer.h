#pragma once

#include "Core/Layer.h"

#include "ChessBoard.h"
#include "Clock.h"
#include "MoveQueue.h"

#include "ChessPlayers/AllPlayer.h"

#include <memory>
#include <atomic>
#include <thread>

class GameManagerLayer : public ApplicationCore::Layer
{
public:
	GameManagerLayer() = default;
	~GameManagerLayer();

	virtual void OnEvent(ApplicationCore::Event& event) override {};
	virtual void OnUpdate(float deltaTime) override;
	virtual void OnRender() override {};

	template<typename TPlayer1, typename TPlayer2>
	void SetPlayerTypes();

	void StartGame();
	void StopGame();

private:
	void RunGame(std::stop_token stopToken, NeraChessEngine::ChessBoard board);
	void Reset();

private:

	NeraChessEngine::Clock m_Clock{};
	NeraChessEngine::ChessBoard m_ChessBoard{};

	NeraChessEngine::MoveQueue m_MoveQueue;

	std::unique_ptr<ChessPlayer> m_Player1 = std::make_unique<Human>();
	std::unique_ptr<ChessPlayer> m_Player2 = std::make_unique<NeraChessBot>();

	bool m_Player1IsWhite = true;
	std::atomic<bool> m_Player1Turn = true;

	std::atomic<bool> m_GameStarted = false;
	std::atomic<bool> m_GameStopRequested = false;
	std::jthread m_GameThread;
};

