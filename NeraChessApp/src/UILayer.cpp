#include "UILayer.h"

#include "GameManagerLayer.h"

#include "Core/Application.h"

#include "imgui.h"

void UILayer::OnRender()
{
	ImGui::Begin("Nera Chess");

	if (ImGui::Button("Start Game"))
	{
		NeraCore::Application::Get().GetLayer<GameManagerLayer>()->StartGame();
	}

	ImGui::End();
}
