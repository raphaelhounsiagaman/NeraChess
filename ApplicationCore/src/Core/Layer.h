#pragma once

#include "Event.h"

namespace ApplicationCore
{
class Layer
{
  public:
	virtual ~Layer() = default;

	virtual void OnEvent(Event&) {}

	virtual void OnUpdate(float) {}
	virtual void OnRender() {}
};
} // namespace ApplicationCore
