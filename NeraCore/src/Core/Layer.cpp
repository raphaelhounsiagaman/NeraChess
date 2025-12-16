#include "Layer.h"

#include "Application.h"

namespace NeraCore
{
	void Layer::QueueTransition(std::unique_ptr<Layer> toLayer)
	{
		// TODO: don't do this
		auto& layerStack = Application::Get().m_LayerStack;
		for (auto& layer : layerStack)
		{
			if (layer.get() == this)
			{
				layer = std::move(toLayer);
				return;
			}
		}

	}


}