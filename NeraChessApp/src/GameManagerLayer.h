#pragma once

#include "ChessBoard.h"
#include "ChessPlayers/Bot/NeraChessBot.h"
#include "ChessPlayers/ChessPlayer.h"
#include "ChessPlayers/Human.h"
#include "Clock.h"
#include "Core/Layer.h"
#include "MoveQueue.h"

#include <atomic>
#include <memory>
#include <thread>

enum class GameResult : uint8_t
{
	NotStarted,
	InProgress,
	WhiteWon,
	BlackWon,
	Draw,
	Aborted,
};

enum class GameEndReason : uint8_t
{
	None,
	Checkmate,
	Timeout,
	Resignation,
	Stalemate,
	Repetition,
	FiftyMoveRule,
	InsufficientMaterial,
	DrawAgreement,
	IllegalMove,
	Stopped,
};

struct GameSnapshot
{
	NeraChessEngine::ClockSnapshot clock;
	NeraChessEngine::TimeControl timeControl;
	GameResult result = GameResult::NotStarted;
	GameEndReason endReason = GameEndReason::None;
	bool gameStarted = false;
	bool player1IsWhite = true;
	bool player1Turn = true;
};

class GameManagerLayer : public ApplicationCore::Layer
{
  public:
	GameManagerLayer() = default;
	~GameManagerLayer();

	void OnEvent(ApplicationCore::Event&) override {}
	void OnUpdate(float) override;
	void OnRender() override {}

	void StartGame();
	void StopGame();
	bool SetTimeControl(NeraChessEngine::TimeControl timeControl);
	GameSnapshot GetSnapshot() const;

  private:
	void RunGame(std::stop_token stopToken, NeraChessEngine::ChessBoard board);
	void SetBoardResult(uint16_t gameOverFlags, bool movingSideWasWhite);
	void Reset();

  private:
	NeraChessEngine::Clock m_Clock{NeraChessEngine::MainTimeControls[2].timeControl};
	NeraChessEngine::ChessBoard m_ChessBoard{};

	NeraChessEngine::MoveQueue m_MoveQueue;

	std::unique_ptr<ChessPlayer> m_Player1 = std::make_unique<Human>();
	std::unique_ptr<ChessPlayer> m_Player2 = std::make_unique<NeraChessBot>();

	std::atomic<bool> m_Player1IsWhite = true;
	std::atomic<bool> m_Player1Turn = true;

	std::atomic<bool> m_GameStarted = false;
	std::atomic<bool> m_GameStopRequested = false;
	std::atomic<bool> m_SwapColorsOnNextGame = false;
	std::atomic<GameResult> m_GameResult = GameResult::NotStarted;
	std::atomic<GameEndReason> m_GameEndReason = GameEndReason::None;
	std::jthread m_GameThread;
};
