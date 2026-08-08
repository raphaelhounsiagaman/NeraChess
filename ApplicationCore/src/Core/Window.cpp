#include "Window.h"

#include "InputEvents.h"
#include "SDL.h"
#include "SDL_image.h"
#include "WindowEvents.h"

#include <stdexcept>
#include <string>

namespace ApplicationCore
{

Window::Window(const WindowSpecification& specification) : m_Specification(specification)
{
	if (m_Specification.Title.empty())
		m_Specification.Title = "Nera Chess Application";
}

Window::~Window()
{
	Destroy();
}

void Window::Create()
{

	uint32_t sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS | SDL_INIT_AUDIO;

	if (SDL_Init(sdlFlags))
	{
		throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
	}
	m_SDLInitialized = true;

	uint32_t windowFlags = SDL_WINDOW_ALLOW_HIGHDPI;

	if (m_Specification.IsResizeable)
		windowFlags |= SDL_WINDOW_RESIZABLE;

	m_SDLWindow = SDL_CreateWindow(m_Specification.Title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
								   m_Specification.Width, m_Specification.Height, windowFlags);

	if (!m_SDLWindow)
	{
		throw std::runtime_error(std::string("Window creation failed: ") + SDL_GetError());
	}
	m_Specification.RendererSpec.Window = m_SDLWindow;
	m_Specification.RendererSpec.VSync = m_Specification.VSync;

	m_Renderer.Init(m_Specification.RendererSpec);
	m_SoundPlayer.Init(m_Specification.SoundPlayerSpec);
}

void Window::Destroy()
{
	if (m_SDLWindow)
	{
		m_Renderer.Destroy();
		m_SoundPlayer.Destroy();
		SDL_DestroyWindow(m_SDLWindow);
		m_SDLWindow = nullptr;
	}

	if (m_SDLInitialized)
	{
		SDL_Quit();
		m_SDLInitialized = false;
	}
}

void Window::Update()
{
	m_Renderer.Present();
}

void Window::PollEvents()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		ImGui_ImplSDL2_ProcessEvent(&event);
		switch (event.type)
		{
		case SDL_QUIT:
		{
			m_ShouldClose = true;
			WindowClosedEvent closeEvent;
			RaiseEvent(closeEvent);
			break;
		}
		case SDL_WINDOWEVENT:
		{
			if (event.window.event == SDL_WINDOWEVENT_RESIZED)
			{
				WindowResizeEvent resizeEvent((uint32_t)event.window.data1, (uint32_t)event.window.data2);
				RaiseEvent(resizeEvent);
			}
			break;
		}
		case SDL_KEYDOWN:
		{
			if (m_Renderer.GetIO()->WantCaptureKeyboard)
				break;
			KeyPressedEvent keyPressEvent(event.key.keysym.sym, event.key.repeat != 0);
			RaiseEvent(keyPressEvent);
			break;
		}
		case SDL_KEYUP:
		{
			if (m_Renderer.GetIO()->WantCaptureKeyboard)
				break;
			KeyReleasedEvent keyReleaseEvent(event.key.keysym.sym);
			RaiseEvent(keyReleaseEvent);
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		{
			if (m_Renderer.GetIO()->WantCaptureMouse)
				break;
			MouseButtonPressedEvent mouseButtonPressedEvent(event.button.button);
			RaiseEvent(mouseButtonPressedEvent);
			break;
		}
		case SDL_MOUSEBUTTONUP:
		{
			if (m_Renderer.GetIO()->WantCaptureMouse)
				break;
			MouseButtonReleasedEvent mouseButtonReleasedEvent(event.button.button);
			RaiseEvent(mouseButtonReleasedEvent);
			break;
		}
		case SDL_MOUSEMOTION:
		{
			if (m_Renderer.GetIO()->WantCaptureMouse)
				break;
			MouseMovedEvent mouseMovedEvent(event.motion.x, event.motion.y);
			RaiseEvent(mouseMovedEvent);
			break;
		}
		case SDL_MOUSEWHEEL:
		{
			if (m_Renderer.GetIO()->WantCaptureMouse)
				break;
			MouseScrolledEvent mouseScrolledEvent(event.wheel.x, event.wheel.y);
			RaiseEvent(mouseScrolledEvent);
			break;
		}
		}
	}
}

void Window::RaiseEvent(Event& event)
{
	if (m_Specification.EventCallback)
		m_Specification.EventCallback(event);
}

Vec2<uint32_t> Window::GetSize() const
{
	int width, height;
	SDL_GetWindowSize(m_SDLWindow, &width, &height);
	return Vec2<uint32_t>((uint32_t)width, (uint32_t)height);
}

} // namespace ApplicationCore
