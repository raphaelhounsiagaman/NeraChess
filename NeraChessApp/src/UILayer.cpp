#include "UILayer.h"

#include "GameManagerLayer.h"
#include "UILayout.h"

#include "Core/Application.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>

namespace
{
	using NeraChessEngine::ClockDuration;

	std::string FormatClock(ClockDuration remaining)
	{
		const int64_t milliseconds = std::max<int64_t>(0, remaining.count());
		if (milliseconds < 10'000)
		{
			const int64_t tenths = (milliseconds + 99) / 100;
			return std::format("{}:{:02}.{}", tenths / 600,
				(tenths / 10) % 60, tenths % 10);
		}

		const int64_t totalSeconds = (milliseconds + 999) / 1000;
		const int64_t hours = totalSeconds / 3600;
		const int64_t minutes = (totalSeconds / 60) % 60;
		const int64_t seconds = totalSeconds % 60;
		if (hours > 0)
			return std::format("{}:{:02}:{:02}", hours, minutes, seconds);
		return std::format("{}:{:02}", totalSeconds / 60, seconds);
	}

	const char* ResultText(GameResult result)
	{
		switch (result)
		{
			case GameResult::NotStarted: return "Choose a time control and start";
			case GameResult::InProgress: return "Game in progress";
			case GameResult::WhiteWon: return "White wins";
			case GameResult::BlackWon: return "Black wins";
			case GameResult::Draw: return "Draw";
			case GameResult::Aborted: return "Game stopped";
		}
		return "";
	}

	const char* ReasonText(GameEndReason reason)
	{
		switch (reason)
		{
			case GameEndReason::None: return "";
			case GameEndReason::Checkmate: return "Checkmate";
			case GameEndReason::Timeout: return "Time expired";
			case GameEndReason::Resignation: return "Resignation";
			case GameEndReason::Stalemate: return "Stalemate";
			case GameEndReason::Repetition: return "Threefold repetition";
			case GameEndReason::FiftyMoveRule: return "50-move rule";
			case GameEndReason::InsufficientMaterial: return "Insufficient material";
			case GameEndReason::DrawAgreement: return "Draw agreement";
			case GameEndReason::IllegalMove: return "Illegal move";
			case GameEndReason::Stopped: return "Stopped by player";
		}
		return "";
	}

	void DrawClockCard(const char* id, const char* playerLabel,
		ClockDuration remaining, bool active, bool expired)
	{
		const ImVec4 background = expired
			? ImVec4(0.42f, 0.10f, 0.10f, 1.0f)
			: (active ? ImVec4(0.12f, 0.34f, 0.27f, 1.0f)
				: ImVec4(0.11f, 0.15f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
		ImGui::PushStyleColor(ImGuiCol_Border,
			active ? ImVec4(0.43f, 0.86f, 0.63f, 1.0f) : ImVec4(0.25f, 0.31f, 0.34f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, active ? 2.0f : 1.0f);

		ImGui::BeginChild(id, ImVec2(0.0f, 104.0f), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted(playerLabel);
		ImGui::SetWindowFontScale(2.15f);
		const std::string clockText = FormatClock(remaining);
		ImGui::TextUnformatted(clockText.c_str());
		ImGui::SetWindowFontScale(1.0f);
		if (expired)
			ImGui::TextColored(ImVec4(1.0f, 0.66f, 0.60f, 1.0f), "FLAGGED");
		else if (active)
			ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.73f, 1.0f), "CLOCK RUNNING");
		else
			ImGui::TextDisabled("Waiting");
		ImGui::EndChild();

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}
}

void UILayer::OnRender()
{
	GameManagerLayer* gameManager = ApplicationCore::Application::Get().GetLayer<GameManagerLayer>();
	if (!gameManager)
		return;

	const GameSnapshot snapshot = gameManager->GetSnapshot();
	const ImGuiIO& io = ImGui::GetIO();
	const float panelWidth = std::max(1.0f, std::min(NeraChessApp::UILayout::SidePanelWidth,
		io.DisplaySize.x - NeraChessApp::UILayout::OuterPadding * 2.0f));
	const float panelHeight = std::max(1.0f,
		io.DisplaySize.y - NeraChessApp::UILayout::OuterPadding * 2.0f);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelWidth - NeraChessApp::UILayout::OuterPadding,
		NeraChessApp::UILayout::OuterPadding), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.075f, 0.09f, 0.97f));

	constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("Nera Chess", nullptr, windowFlags);

	ImGui::TextColored(ImVec4(0.53f, 0.90f, 0.68f, 1.0f), "NERA CHESS");
	ImGui::TextDisabled("Timed game");
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextUnformatted("Time control");
	size_t selectedPreset = 2;
	for (size_t index = 0; index < NeraChessEngine::MainTimeControls.size(); ++index)
	{
		if (NeraChessEngine::MainTimeControls[index].timeControl == snapshot.timeControl)
		{
			selectedPreset = index;
			break;
		}
	}

	ImGui::BeginDisabled(snapshot.gameStarted);
	if (ImGui::BeginCombo("##TimeControl",
		NeraChessEngine::MainTimeControls[selectedPreset].label.data()))
	{
		for (size_t index = 0; index < NeraChessEngine::MainTimeControls.size(); ++index)
		{
			const bool selected = index == selectedPreset;
			if (ImGui::Selectable(NeraChessEngine::MainTimeControls[index].label.data(), selected))
				gameManager->SetTimeControl(NeraChessEngine::MainTimeControls[index].timeControl);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("Time is assigned to each player");
	ImGui::Spacing();

	ImGui::BeginDisabled(snapshot.gameStarted);
	if (ImGui::Button("Start Game", ImVec2(-1.0f, 38.0f)))
		gameManager->StartGame();
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!snapshot.gameStarted);
	if (ImGui::Button("Stop Game", ImVec2(-1.0f, 32.0f)))
		gameManager->StopGame();
	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::SeparatorText("Game status");
	ImGui::TextUnformatted(ResultText(snapshot.result));
	if (snapshot.endReason != GameEndReason::None)
		ImGui::TextDisabled("%s", ReasonText(snapshot.endReason));
	ImGui::Spacing();

	const bool topIsWhite = !snapshot.player1IsWhite;
	const ClockDuration topTime = topIsWhite
		? snapshot.clock.whiteRemaining : snapshot.clock.blackRemaining;
	const ClockDuration bottomTime = topIsWhite
		? snapshot.clock.blackRemaining : snapshot.clock.whiteRemaining;
	const bool topActive = snapshot.gameStarted && !snapshot.clock.paused &&
		snapshot.clock.whiteActive == topIsWhite;
	const bool bottomActive = snapshot.gameStarted && !snapshot.clock.paused &&
		snapshot.clock.whiteActive != topIsWhite;

	const char* topLabel = topIsWhite ? "White - NeraChess" : "Black - NeraChess";
	const char* bottomLabel = snapshot.player1IsWhite ? "White - You" : "Black - You";
	DrawClockCard("OpponentClock", topLabel, topTime, topActive,
		snapshot.result != GameResult::InProgress && topTime.count() == 0);
	ImGui::Spacing();
	DrawClockCard("PlayerClock", bottomLabel, bottomTime, bottomActive,
		snapshot.result != GameResult::InProgress && bottomTime.count() == 0);

	if (snapshot.clock.increment.count() > 0)
	{
		const auto incrementSeconds = std::chrono::duration_cast<std::chrono::seconds>(
			snapshot.clock.increment).count();
		ImGui::TextDisabled("+%lld seconds after every move",
			static_cast<long long>(incrementSeconds));
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}
