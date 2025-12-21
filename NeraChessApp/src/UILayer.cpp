#include "UILayer.h"

#include "GameManagerLayer.h"

#include "Core/Application.h"

#include "imgui.h"

void UILayer::OnRender()
{
	GameManagerLayer* gameManager = NeraCore::Application::Get().GetLayer<GameManagerLayer>();

	ImGui::Begin("Nera Chess");

	if (ImGui::Button("Start Game"))
	{
		gameManager->StartGame();
	}

	if (ImGui::Button("Stop Game"))
	{
		gameManager->StopGame();
	}

	ImGui::End();
}
