#pragma once

#include "Core/Layer.h"

#include "Core/Application.h"

class BackgroundLayer : public NeraCore::Layer
{
public:
	BackgroundLayer();
	virtual ~BackgroundLayer() = default;

	virtual void OnEvent(NeraCore::Event& event) override {};
	virtual void OnUpdate(float deltaTime) override {};
	virtual void OnRender() override;

private:

	const NeraCore::Color c_BackgroundColor = NeraCore::Color(3, 20, 28);

	NeraCore::Renderer& m_Renderer;


};
