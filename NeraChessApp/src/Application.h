#pragma once

#include "ChessBoard.h"

#include <memory>
#include <thread>
#include <mutex>

#include "InputHandler.h"
#include "Renderer.h"
#include "ChessPLayer.h"

#include "Bot1.h"
#include "Human.h"

class Application
{
public:
	Application();
	~Application() = default;

	void Run();

private:

	template<typename player1Type, typename player2Type>
	void StartGame();

	void ProcessGame();

	void GetMoveFromPlayer(ChessPlayer* player);

private:

	bool m_Running = false;

	InputHandler m_InputHandler{};
	Renderer m_Renderer{};

	ChessBoard m_ChessBoard{};

	std::unique_ptr<ChessPlayer> m_Player1;
	std::unique_ptr<ChessPlayer> m_Player2;

	bool m_Player1IsWhite = false;
	bool m_Player1Turn = false;

	bool m_GameStarted = false;

	std::mutex m_MovePlayedMutex;
	Move m_MovePlayed{0};

};