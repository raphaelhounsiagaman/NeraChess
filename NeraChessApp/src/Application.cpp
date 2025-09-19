#include "Application.h"

#include <iostream>
#include <thread>
#include <typeinfo>

#include "ChessPlayers/AllBots.h"

Application::Application()
{
	m_Running = true;

	m_Renderer.SetInputHandler(m_InputHandler);

	if (m_Renderer.GetError()) 
		m_Running = false;
}

void Application::Run()
{	
	Bitboard showBoard = 0ULL;
	Bitboard possibleMoves = 0ULL;

	//ChessBoard testBoard = ChessBoard();
	//ChessBoard::RunPerformanceTest(testBoard, 5);

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
						StartGame<Human, MyBotOld>();
					else 
						m_GameStopRequested = true;
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
		m_Renderer.Render(m_ChessBoard);
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
	m_Renderer.SetWhiteBottom(m_Player1IsWhite);
	m_Player1Turn = m_Player1IsWhite;

	m_Player1 = std::make_unique<player1Type>();
	m_Player2 = std::make_unique<player2Type>();

	m_GameStarted = true;

	m_MovePlayed = 0;

	m_ChessBoard = ChessBoard();

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

	if (m_GameStopRequested)
	{
		std::cout << "Game stop requested, resetting board.\n\n";
		m_GameStarted = false;
		m_ChessBoard = ChessBoard(); 
		m_Renderer.SetWhiteBottom(true);
		m_GameStopRequested = false;
		return;
	}

	assert(m_Player1 != nullptr && m_Player2 != nullptr);

	std::cout << "Move Played: " 
		<< SquareUtil::SquareAsString(MoveUtil::GetFromSquare(m_MovePlayed)) 
		<< SquareUtil::SquareAsString(MoveUtil::GetTargetSquare(m_MovePlayed))
		<< "\n";

	m_ChessBoard.MakeMove(m_MovePlayed, true);

	uint16_t gameOverFlags = m_ChessBoard.GetGameOver(true);
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

		std::cout << "Game over, " << (m_Player1Turn ? "Player1" : "Player2") << " ends the game by " << gameOverReason << "\n\n";
		m_GameStarted = false;
		return;
	}

	m_Player1Turn = !m_Player1Turn;
	m_MovePlayed = Move(0);

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();

	std::thread getMoveThread(&Application::GetMoveFromPlayer, this, currentPlayer);
	getMoveThread.detach();		
}

void Application::GetMoveFromPlayer(ChessPlayer* player)
{
	Move move = player->GetNextMove(m_ChessBoard, m_Timer);
	std::lock_guard<std::mutex> lock(m_MovePlayedMutex);
	m_MovePlayed = move;
	MoveList legalMoves = m_ChessBoard.GetLegalMoves();
	if (std::find(legalMoves.begin(), legalMoves.end(), m_MovePlayed) == legalMoves.end())
	{
		std::cout << "Player wants to play illegal move: " << move << "\n";
		m_ChessBoard = ChessBoard();
		m_GameStarted = false;
	}
}




