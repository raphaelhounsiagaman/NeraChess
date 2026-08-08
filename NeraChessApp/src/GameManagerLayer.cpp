#include "GameManagerLayer.h"

#include "BoardLayer.h"
#include "Core/Application.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <print>
#include <thread>
#include <utility>

GameManagerLayer::~GameManagerLayer()
{
	StopGame();
	if (m_GameThread.joinable())
		m_GameThread.join();
}

void GameManagerLayer::OnUpdate(float)
{

	NeraChessEngine::Move move{0};

	while (m_MoveQueue.TryPop(move))
	{
		m_ChessBoard.MakeMove(move, true);

		BoardLayer* boardLayer = ApplicationCore::Application::Get().GetLayer<BoardLayer>();
		if (boardLayer)
			boardLayer->PlayMove(move);
	}
}

bool GameManagerLayer::SetTimeControl(NeraChessEngine::TimeControl timeControl)
{
	if (m_GameStarted)
		return false;
	m_Clock.SetTimeControl(timeControl);
	return true;
}

GameSnapshot GameManagerLayer::GetSnapshot() const
{
	return {
		.clock = m_Clock.GetSnapshot(),
		.timeControl = m_Clock.GetTimeControl(),
		.result = m_GameResult.load(),
		.endReason = m_GameEndReason.load(),
		.gameStarted = m_GameStarted.load(),
		.player1IsWhite = m_Player1IsWhite.load(),
		.player1Turn = m_Player1Turn.load(),
	};
}

void GameManagerLayer::StartGame()
{
	if (m_GameStarted)
		return;

	if (m_GameThread.joinable())
		m_GameThread.join();

	if (m_GameStarted.exchange(true))
		return;

	if (m_SwapColorsOnNextGame.exchange(false))
		m_Player1IsWhite = !m_Player1IsWhite.load();

	m_GameStopRequested = false;
	m_GameResult = GameResult::InProgress;
	m_GameEndReason = GameEndReason::None;

	m_ChessBoard = NeraChessEngine::ChessBoard();

	BoardLayer* boardLayer = ApplicationCore::Application::Get().GetLayer<BoardLayer>();
	if (boardLayer)
	{
		boardLayer->SetChessBoard(m_ChessBoard);
		boardLayer->SetWhiteBottom(m_Player1IsWhite.load());
	}

	m_Player1Turn =
		m_Player1IsWhite.load() == m_ChessBoard.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove);

	m_MoveQueue.Clear();

	std::println("Game started, {} starts.", m_Player1Turn ? "Player 1" : "Player 2");

	m_GameThread = std::jthread([this, board = m_ChessBoard](std::stop_token stopToken) mutable
								{ RunGame(stopToken, std::move(board)); });
}

void GameManagerLayer::StopGame()
{
	if (m_GameStopRequested || !m_GameStarted)
		return;

	m_GameStopRequested = true;
	m_GameThread.request_stop();

	ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();
	currentPlayer->StopSearching();
}

void GameManagerLayer::RunGame(std::stop_token stopToken, NeraChessEngine::ChessBoard board)
{
	const bool whiteStarts = board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove);
	m_Clock.Start(whiteStarts);

	while (!stopToken.stop_requested() && !m_GameStopRequested)
	{

		ChessPlayer* currentPlayer = m_Player1Turn ? m_Player1.get() : m_Player2.get();
		const bool whiteToMove = board.GetBoardState().HasFlag(NeraChessEngine::BoardStateFlags::WhiteToMove);

		std::atomic<bool> moveFinished = false;
		std::jthread flagWatcher(
			[this, currentPlayer, whiteToMove, &moveFinished](std::stop_token watchToken)
			{
				using namespace std::chrono_literals;
				while (!watchToken.stop_requested() && !moveFinished && !m_GameStopRequested)
				{
					const auto remaining = m_Clock.GetRemaining(whiteToMove);
					if (remaining.count() == 0)
					{
						currentPlayer->StopSearching();
						return;
					}
					std::this_thread::sleep_for(std::min(remaining, 10ms));
				}
			});

		NeraChessEngine::Move move = currentPlayer->GetNextMove(board, m_Clock);
		moveFinished = true;
		m_Clock.Pause();
		flagWatcher.request_stop();
		flagWatcher.join();

		if (stopToken.stop_requested() || m_GameStopRequested)
		{
			std::println("Game Aborted!");
			break;
		}

		if (m_Clock.HasExpired(whiteToMove))
		{
			m_GameResult = whiteToMove ? GameResult::BlackWon : GameResult::WhiteWon;
			m_GameEndReason = GameEndReason::Timeout;
			std::println("Game over, {} loses on time.", whiteToMove ? "White" : "Black");
			break;
		}

		NeraChessEngine::MoveList<218> legalMoves = board.GetLegalMoves();
		if (std::find(legalMoves.begin(), legalMoves.end(), move) == legalMoves.end())
		{
			m_GameResult = whiteToMove ? GameResult::BlackWon : GameResult::WhiteWon;
			m_GameEndReason = GameEndReason::IllegalMove;
			std::println("Player wants to play illegal move: {}", move.ToUCI());
			break;
		}

		board.MakeMove(move, true);

		m_MoveQueue.Push(move);

		std::println("Move Played: {}{}", move.GetStartSquare().ToString(), move.GetTargetSquare().ToString());

		uint16_t gameOverFlags = board.GetGameOver(true);
		if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_GAME_OVER)
		{
			SetBoardResult(gameOverFlags, whiteToMove);
			break;
		}

		if (!m_Clock.Press())
		{
			m_GameResult = whiteToMove ? GameResult::BlackWon : GameResult::WhiteWon;
			m_GameEndReason = GameEndReason::Timeout;
			break;
		}
		m_Clock.Resume();

		m_Player1Turn = !m_Player1Turn;
	}

	m_Clock.Stop();
	if (m_GameResult == GameResult::InProgress)
	{
		m_GameResult = GameResult::Aborted;
		m_GameEndReason = GameEndReason::Stopped;
	}
	Reset();
}

void GameManagerLayer::SetBoardResult(uint16_t gameOverFlags, bool movingSideWasWhite)
{
	if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_CHECKMATE)
	{
		m_GameResult = movingSideWasWhite ? GameResult::WhiteWon : GameResult::BlackWon;
		m_GameEndReason = GameEndReason::Checkmate;
	}
	else if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_RESIGN)
	{
		m_GameResult = movingSideWasWhite ? GameResult::BlackWon : GameResult::WhiteWon;
		m_GameEndReason = GameEndReason::Resignation;
	}
	else if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_TIMEOUT)
	{
		m_GameResult = movingSideWasWhite ? GameResult::BlackWon : GameResult::WhiteWon;
		m_GameEndReason = GameEndReason::Timeout;
	}
	else
	{
		m_GameResult = GameResult::Draw;
		if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_STALEMATE)
			m_GameEndReason = GameEndReason::Stalemate;
		else if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_REPETITION)
			m_GameEndReason = GameEndReason::Repetition;
		else if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_50MOVE_RULE)
			m_GameEndReason = GameEndReason::FiftyMoveRule;
		else if (gameOverFlags & (uint16_t)NeraChessEngine::GameOverFlags::IS_INSUFFICIENT_MATERIAL)
			m_GameEndReason = GameEndReason::InsufficientMaterial;
		else
			m_GameEndReason = GameEndReason::DrawAgreement;
	}

	std::println("Game over: result {}, reason {}.", static_cast<int>(m_GameResult.load()),
				 static_cast<int>(m_GameEndReason.load()));
}

void GameManagerLayer::Reset()
{
	std::println("Everything is reset!");

	m_GameStarted = false;
	m_GameStopRequested = false;

	m_Player1->ResetGame();
	m_Player2->ResetGame();

	m_SwapColorsOnNextGame = true;
}
