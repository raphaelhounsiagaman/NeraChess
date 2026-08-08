#include "Application.h"

#include <chrono>
#include <ranges>
#include <stdexcept>

namespace ApplicationCore
{

static Application* s_Application = nullptr;

Application::Application(const ApplicationSpecification& appSpec) : m_Specification(appSpec)
{
	if (s_Application)
		throw std::logic_error("Only one Application instance can exist at a time");

	s_Application = this;

	if (m_Specification.WindowSpec.Title.empty())
		m_Specification.WindowSpec.Title = m_Specification.Name;

	m_Specification.WindowSpec.EventCallback = [this](Event& event) { EmitEvent(event); };

	try
	{
		m_Window = std::make_unique<Window>(m_Specification.WindowSpec);
		m_Window->Create();
	}
	catch (...)
	{
		s_Application = nullptr;
		throw;
	}
}

Application::~Application()
{
	// Layers can own worker threads and SDL resources. Destroy them in reverse
	// stacking order while the window/subsystems are still alive.
	while (!m_LayerStack.empty())
		m_LayerStack.pop_back();

	m_Window.reset();

	s_Application = nullptr;
}

void Application::Run()
{
	m_Running = true;

	auto lastTime = std::chrono::steady_clock::now();

	while (m_Running)
	{

		m_Window->PollEvents();

		if (m_Window->ShouldClose())
		{
			Stop();
			break;
		}

		auto currentTime = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
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
	if (!s_Application)
		throw std::logic_error("Application::Get() called without an active application");
	return *s_Application;
}

} // namespace ApplicationCore
