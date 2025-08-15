#include "Application.h"

#include "ChessBoard.h"

#include <iostream>
#include <thread>
#include <typeinfo>

#include "InputHandler.h"
#include "Renderer.h"

Application::Application()
{
	m_Running = true;

	m_Renderer.SetChessBoard(&m_ChessBoard);
	m_Renderer.SetInputHandler(m_InputHandler);

	if (m_Renderer.GetError()) 
		m_Running = false;
}

void Application::Run()
{	
	Bitboard showBoard = 0ULL;
	Bitboard possibleMoves = 0ULL;

	//ChessBoard::RunPerformanceTest(ChessBoard(), 5);

	while (m_Running)
	{
		m_InputHandler.Process();


		InputEvent event;
		while (m_InputHandler.PollInputEvent(event))
		{
			uint8_t square;

			switch (event.type)
			{
				case EventTypeQuit:
					m_Running = false;
					break;
				case EventTypeWindowResize:
					m_Renderer.UpdateWindowSize();
					break;
				case EventTypeKeyPressed:
					break;
				case EventTypeRMBPressed:
					square = m_Renderer.GetSquareFromPos(event.eventPos.x, event.eventPos.y);
					if (square < 64)
						showBoard ^= 1ULL << square;
					break;
				case EventTypeLMBPressed:
					showBoard = 0ULL;
					square = m_Renderer.GetSquareFromPos(event.eventPos.x, event.eventPos.y);
					if (square < 64)
					{
						if (Human* human = dynamic_cast<Human*>(m_Player1.get()))
						{
							human->SetSelectedSquare(square);
						}
					}
					
					break;
				case EventTypeStartGame:
					if (!m_GameStarted)
						StartGame<Human, Bot1>();
					break;
				case EventTypeStopGame:
					
					break;
				default:
					break;

			}
		}

		if (Human* human = dynamic_cast<Human*>(m_Player1.get()))
		{
			possibleMoves = human->GetPossibleMoves();
		}

		if (m_GameStarted)
			ProcessGame();

		m_Renderer.SetBitboard(showBoard ? showBoard : possibleMoves);
		m_Renderer.Render();
	}
}

template<typename player1Type, typename player2Type>
void Application::StartGame()
{
	static_assert(std::is_base_of<ChessPlayer, player1Type>::value,
		"player1Type must be derived from BasePlayerTypeClass");
	static_assert(std::is_base_of<ChessPlayer, player2Type>::value,
		"player2Type must be derived from BasePlayerTypeClass");

	m_Player1IsWhite = !m_Player1IsWhite;
	m_Player1Turn = m_Player1IsWhite;

	m_Player1 = std::make_unique<player1Type>();
	m_Player2 = std::make_unique<player2Type>();

	m_GameStarted = true;

	m_MovePlayed = Move(0);

	m_ChessBoard = ChessBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();

	std::thread getMoveThread(&Application::GetMoveFromPlayer, this, currentPlayer);
	if (getMoveThread.joinable())
		getMoveThread.detach();

	std::cout << "Game started, " << (m_Player1Turn ? "Player1" : "Player2") << " starts.\n";
}

void Application::ProcessGame()
{
	if (!m_MovePlayed)
		return;

	assert(m_Player1 != nullptr && m_Player2 != nullptr);
	MoveList legalMoves = m_ChessBoard.GetLegalMoves();
	std::unique_lock<std::mutex> lock(m_MovePlayedMutex);
	assert(std::find(legalMoves.begin(), legalMoves.end(), m_MovePlayed) != legalMoves.end());

	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	m_ChessBoard.MakeMove(m_MovePlayed);
	lock.unlock();

	uint16_t gameOverFlags = m_ChessBoard.GetGameOverFlags();
	if (gameOverFlags & (uint16_t)GameOverFlags::IS_GAME_OVER)
	{
		std::string gameOverReason = "";

		if (gameOverFlags & (uint16_t)GameOverFlags::IS_CHECKMATE)
			gameOverReason = "Checkmate";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_RESIGN)
			gameOverReason = "Resignation";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_TIMEOUT)
			gameOverReason = "Timout";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_STALEMATE)
			gameOverReason = "Stalemate";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_REPETITION)
			gameOverReason = "Repetition";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_50MOVE_RULE)
			gameOverReason = "50 Move Rule";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_INSUFFICIENT_MATERIAL)
			gameOverReason = "Insufficient Material";
		else if (gameOverFlags & (uint16_t)GameOverFlags::IS_AGREE_ON_DRAW)
			gameOverReason = "Agree on Draw";

		std::cout << "Game over, " << (m_Player1Turn ? "Player1" : "Player2") << " wins by " << gameOverReason << "\n";
		m_GameStarted = false;
		return;
	}

	m_Player1Turn = !m_Player1Turn;

	std::unique_lock<std::mutex> lock2(m_MovePlayedMutex);
	m_MovePlayed = Move(0);
	lock2.unlock();

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();

	std::thread getMoveThread(&Application::GetMoveFromPlayer, this, currentPlayer);
	if (getMoveThread.joinable())
		getMoveThread.detach();


}

void Application::GetMoveFromPlayer(ChessPlayer* player)
{
	Move move = player->GetNextMove(m_ChessBoard);
	std::lock_guard<std::mutex> lock(m_MovePlayedMutex);
	m_MovePlayed = move;
}




