#pragma once

#include "Core/Application.h"
#include "Core/Layer.h"

class BackgroundLayer : public ApplicationCore::Layer
{
  public:
	BackgroundLayer();
	virtual ~BackgroundLayer() = default;

	void OnEvent(ApplicationCore::Event&) override {}
	void OnUpdate(float) override {}
	void OnRender() override;

  private:
	const ApplicationCore::Color c_BackgroundColor = ApplicationCore::Color(3, 20, 28);

	ApplicationCore::Renderer& m_Renderer;
};
