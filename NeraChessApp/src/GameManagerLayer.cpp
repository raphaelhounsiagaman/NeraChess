#include "GameManagerLayer.h"

#include "Core/Application.h"

#include "BoardLayer.h"

#include <thread>
#include <print>

template<typename TPlayer1, typename TPlayer2>
void GameManagerLayer::SetPlayerTypes()
{
	m_Player1 = std::make_unique<TPlayer1>();
	m_Player2 = std::make_unique<TPlayer2>();
}

void GameManagerLayer::OnUpdate(float deltaTime)
{

	ChessCore::Move move{0};

	while (m_MoveQueue.Pop(&move))
	{
		m_ChessBoard.MakeMove(move);

		BoardLayer* boardLayer = NeraCore::Application::Get().GetLayer<BoardLayer>();
		if (boardLayer)
			boardLayer->PlayMove(move);
	}

}

void GameManagerLayer::StartGame()
{
	if (m_GameStarted)
		return;

	m_ChessBoard = ChessCore::ChessBoard();

	BoardLayer* boardLayer = NeraCore::Application::Get().GetLayer<BoardLayer>();
	if (boardLayer)
	{
		boardLayer->SetChessBoard(m_ChessBoard);
		boardLayer->SetWhiteBottom(m_Player1IsWhite);
	}

	m_Player1Turn = m_Player1IsWhite == m_ChessBoard.GetBoardState().HasFlag(ChessCore::BoardStateFlags::WhiteToMove);

	m_MoveQueue.Clear();

	std::println("Game started, {} starts.", m_Player1Turn ? "Player 1" : "Player 2");

	std::thread gameThread(&GameManagerLayer::RunGame, this, m_ChessBoard);
	if (gameThread.joinable())
		gameThread.detach();
}


void GameManagerLayer::StopGame()
{
	if (m_GameStopRequested || !m_GameStarted)
		return;

	m_GameStopRequested = true;

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();
	currentPlayer->StopSearching();
}

void GameManagerLayer::RunGame(ChessCore::ChessBoard board)
{
	m_Clock.Start();
	m_GameStarted = true;

	while (m_GameStarted)
	{

		ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();
		
		ChessCore::Move move = currentPlayer->GetNextMove(board, m_Clock);

		m_Clock.Pause();

		if (m_GameStopRequested)
		{
			std::println("Game Aborted!");
			break;
		}

		ChessCore::MoveList<218> legalMoves = board.GetLegalMoves();
		if (std::find(legalMoves.begin(), legalMoves.end(), move) == legalMoves.end())
		{
			std::println("Player wants to play illegal move: {}", move);
			break;
		}

		board.MakeMove(move);

		m_MoveQueue.Push(move);

		std::println("Move Played: {}{}",
			ChessCore::SquareUtil::SquareAsString(ChessCore::MoveUtil::GetFromSquare(move)),
			ChessCore::SquareUtil::SquareAsString(ChessCore::MoveUtil::GetTargetSquare(move)));

		uint16_t gameOverFlags = board.GetGameOver(true);
		if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_GAME_OVER)
		{
			std::string gameOverReason = "";

			if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_CHECKMATE)
				gameOverReason = "Checkmate";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_RESIGN)
				gameOverReason = "Resignation";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_TIMEOUT)
				gameOverReason = "Timout";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_STALEMATE)
				gameOverReason = "Stalemate";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_REPETITION)
				gameOverReason = "Repetition";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_50MOVE_RULE)
				gameOverReason = "50 Move Rule";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_INSUFFICIENT_MATERIAL)
				gameOverReason = "Insufficient Material";
			else if (gameOverFlags & (uint16_t)ChessCore::GameOverFlags::IS_AGREE_ON_DRAW)
				gameOverReason = "Agree on Draw";

			std::print("Game over, {} ends the game by {} \n\n", (m_Player1Turn ? "Player 1" : "Player 2"), gameOverReason);

			//std::cout << m_ChessBoard.GetSANMoveList() << "\n\n"; TODO: enable san move list printing
			break;
		}

		m_Clock.Resume();
		m_Clock.Press();

		m_Player1Turn = !m_Player1Turn;
	}

	Reset();
}


void GameManagerLayer::Reset()
{
	std::println("Everything is reset!");

	m_GameStarted = false;
	m_GameStopRequested = false;

	m_Player1->ResetGame();
	m_Player2->ResetGame();

	m_Player1IsWhite = !m_Player1IsWhite;
}

