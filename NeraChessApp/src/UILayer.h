#pragma once

#include "Core/Layer.h"
#include "GameManagerLayer.h"

class UILayer : public ApplicationCore::Layer
{
  public:
	UILayer() = default;
	virtual ~UILayer() = default;
	void OnEvent(ApplicationCore::Event&) override {}
	void OnUpdate(float) override {}
	void OnRender() override;

  private:
};
