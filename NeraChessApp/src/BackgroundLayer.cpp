#include "BackgroundLayer.h"

BackgroundLayer::BackgroundLayer()
	: m_Renderer(ApplicationCore::Application::Get().GetWindow()->GetRenderer())
{
}

void BackgroundLayer::OnRender()
{
	m_Renderer.Clear(c_BackgroundColor);
}