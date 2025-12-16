#pragma once

#include "Core/Layer.h"

#include "ChessBoard.h"
#include "Timer.h"

#include "ChessPlayers/AllPlayer.h"

#include <memory>
#include <mutex>

class GameManagerLayer : public NeraCore::Layer
{
public:
	GameManagerLayer() = default;
	virtual ~GameManagerLayer() = default;

	virtual void OnEvent(NeraCore::Event& event) override {};
	virtual void OnUpdate(float deltaTime) override;
	virtual void OnRender() override {};

	template<typename TPlayer1, typename TPlayer2, typename... Args1, typename... Args2>
	void SetPlayerTypes(Args1&&... args1, Args2&&... args2);

	void StartGame();

	void GetMoveFromPlayer(ChessPlayer* player);

	void StopGame();

private:

private:

	ChessCore::Timer m_Timer{};
	ChessCore::ChessBoard m_ChessBoard{};

	std::mutex m_MovePlayedMutex;
	ChessCore::Move m_MovePlayed = 0;

	std::unique_ptr<ChessPlayer> m_Player1 = std::make_unique<Human>();
	std::unique_ptr<ChessPlayer> m_Player2 = std::make_unique<NeraChessBot>();

	bool m_Player1IsWhite = false;
	bool m_Player1Turn = true;

	bool m_GameStarted = false;
	bool m_GameStopRequested = false;
};


