#pragma once

#include "Core/Layer.h"

#include "Core/Application.h"

class BackgroundLayer : public ApplicationCore::Layer
{
public:
	BackgroundLayer();
	virtual ~BackgroundLayer() = default;

	virtual void OnEvent(ApplicationCore::Event& event) override {};
	virtual void OnUpdate(float deltaTime) override {};
	virtual void OnRender() override;

private:

	const ApplicationCore::Color c_BackgroundColor = ApplicationCore::Color(3, 20, 28);

	ApplicationCore::Renderer& m_Renderer;


};
