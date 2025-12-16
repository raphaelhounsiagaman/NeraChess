#include "GameManagerLayer.h"

#include "Core/Application.h"

#include "BoardLayer.h"

#include <thread>
#include <print>


template<typename TPlayer1, typename TPlayer2, typename ...Args1, typename ...Args2>
void GameManagerLayer::SetPlayerTypes(Args1&&... args1, Args2&&... args2)
{
	m_Player1 = std::make_unique<TPlayer1>(std::forward<Args1>(args1)...);
	m_Player2 = std::make_unique<TPlayer2>(std::forward<Args2>(args2)...);
}

void GameManagerLayer::StartGame()
{
	if (m_GameStarted)
		return;

	// Initialize the chess board to the starting position
	//m_ChessBoard = ChessCore::ChessBoard("4r2r/Bp1k1ppp/2pp4/6bP/6b1/2PB4/PPP2PP1/R4K1R b - - 2 16");
	m_ChessBoard = ChessCore::ChessBoard("r3kb1r/ppq2ppp/2p5/3Pp3/4n1b1/1PNP1N2/P1P2PPP/R1BQ1RK1 w kq - 1 11");
	BoardLayer* boardLayer = NeraCore::Application::Get().GetLayer<BoardLayer>();
	if (boardLayer)
		boardLayer->SetChessBoard(m_ChessBoard);
	// Start the timer for both players
	
	m_Player1IsWhite = !m_Player1IsWhite;
	if (boardLayer)
		boardLayer->SetWhiteBottom(m_Player1IsWhite);

	m_Player1Turn = !(m_Player1IsWhite != m_ChessBoard.GetBoardState().HasFlag(ChessCore::BoardStateFlags::WhiteToMove));

	m_GameStarted = true;

	m_MovePlayed = 0;
	
	m_Timer.Start();

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();

	m_Timer.Start();

	std::thread getMoveThread(&GameManagerLayer::GetMoveFromPlayer, this, currentPlayer);
	if (getMoveThread.joinable())
		getMoveThread.detach();

	std::println("Game started, {} starts.", (m_Player1Turn ? "Player1" : "Player2"));

}

void GameManagerLayer::GetMoveFromPlayer(ChessPlayer* player)
{
	ChessCore::Move move = player->GetNextMove(m_ChessBoard, m_Timer);
	std::lock_guard<std::mutex> lock(m_MovePlayedMutex);
	ChessCore::MoveList<218> legalMoves = m_ChessBoard.GetLegalMoves();
	if (std::find(legalMoves.begin(), legalMoves.end(), move) == legalMoves.end())
	{
		std::println("Player wants to play illegal move: {}", move);
		assert(false);
		m_GameStarted = false;
		m_MovePlayed = legalMoves[0];
		return;
	}
	m_MovePlayed = move;
}

void GameManagerLayer::StopGame()
{
	m_GameStopRequested = true;

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();
	currentPlayer->StopSearching();
}

void GameManagerLayer::OnUpdate(float deltaTime)
{
	if (!m_MovePlayed)
		return;

	if (m_GameStopRequested)
	{
		m_GameStopRequested = false;

		std::print("Game stop requested, resetting board.\n\n");
		m_GameStarted = false;
		m_ChessBoard = ChessCore::ChessBoard();
		BoardLayer* boardLayer = NeraCore::Application::Get().GetLayer<BoardLayer>();
		boardLayer->SetChessBoard(m_ChessBoard);
		return;
	}

	std::println("Move Played: {}{}", 
		ChessCore::SquareUtil::SquareAsString(ChessCore::MoveUtil::GetFromSquare(m_MovePlayed)),
		ChessCore::SquareUtil::SquareAsString(ChessCore::MoveUtil::GetTargetSquare(m_MovePlayed)));

	m_ChessBoard.MakeMove(m_MovePlayed, true);
	BoardLayer* boardLayer = NeraCore::Application::Get().GetLayer<BoardLayer>();
	boardLayer->PlayMove(m_MovePlayed);

	uint16_t gameOverFlags = m_ChessBoard.GetGameOver(true);
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

		std::print("Game over, {} ends the game by {} \n\n", (m_Player1Turn ? "Player1" : "Player2"), gameOverReason);

		//std::cout << m_ChessBoard.GetSANMoveList() << "\n\n"; TODO: enable san move list printing

		m_GameStarted = false;
		m_MovePlayed = 0;
		return;
	}

	m_Player1Turn = !m_Player1Turn;
	m_MovePlayed = 0;

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();

	std::thread getMoveThread(&GameManagerLayer::GetMoveFromPlayer, this, currentPlayer);
	getMoveThread.detach();

}