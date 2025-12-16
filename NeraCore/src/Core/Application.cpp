#include "Application.h"

#include <ranges>
#include <chrono>

namespace NeraCore
{

	static Application* s_Application = nullptr;

	Application::Application(const ApplicationSpecification& appSpec)
		: m_Specification(appSpec)
	{
		s_Application = this;

		if (m_Specification.WindowSpec.Title.empty())
			m_Specification.WindowSpec.Title = m_Specification.Name;

		m_Specification.WindowSpec.EventCallback = [this](Event& event) { EmitEvent(event); };

		m_Window = std::make_shared<Window>(m_Specification.WindowSpec);
		m_Window->Create();
	}

	Application::~Application()
	{
		m_Window->Destroy();

		s_Application = nullptr;
	}

	void Application::Run()
	{
		using namespace std::chrono;

		m_Running = true;

		auto lastTime = high_resolution_clock::now();

		while (m_Running)
		{

			m_Window->PollEvents();
			
			if (m_Window->ShouldClose())
			{
				Stop();
				break;
			}

			auto currentTime = high_resolution_clock::now();
			float deltaTime = duration<float, seconds::period>(currentTime - lastTime).count();
			lastTime = currentTime;

			for (const std::unique_ptr<Layer>& layer : m_LayerStack)
				layer->OnUpdate(deltaTime);

			for (const std::unique_ptr<Layer>& layer : m_LayerStack)
				layer->OnRender();

			m_Window->Update();
		}

	}

	void Application::Stop()
	{
		m_Running = false;
	}

	void Application::EmitEvent(Event& event)
	{
		for (auto& layer : std::views::reverse(m_LayerStack))
		{
			layer->OnEvent(event);
			if (event.Handled)
				break;
		}
	}

	Application& Application::Get()
	{
		assert(s_Application);
		return *s_Application;
	}

}


