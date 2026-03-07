#pragma once

#include "Core/Layer.h"
#include "GameManagerLayer.h"


class UILayer : public ApplicationCore::Layer
{
public:
	UILayer() = default;
	virtual ~UILayer() = default;
	virtual void OnEvent(ApplicationCore::Event& event) override {};
	virtual void OnUpdate(float ts) override {};
	virtual void OnRender() override;

private:

};
