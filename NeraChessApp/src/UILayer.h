#pragma once

#include "Core/Layer.h"

class UILayer : public NeraCore::Layer
{
public:
	UILayer() {};
	virtual ~UILayer() = default;
	virtual void OnEvent(NeraCore::Event& event) override {};
	virtual void OnUpdate(float ts) override {};
	virtual void OnRender() override;


};
