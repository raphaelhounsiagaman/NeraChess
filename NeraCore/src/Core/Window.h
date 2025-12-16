#pragma once

#include "Event.h"
#include "Renderer/Renderer.h"
#include "Sound/SoundPlayer.h"

#include "SDL.h"
#include "SDL_image.h"

#include <string>
#include <functional>

namespace NeraCore
{
	struct WindowSpecification
	{
		std::string Title;
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool IsResizeable = true;
		bool VSync = true;

		using EventCallbackFn = std::function<void(Event&)>;
		EventCallbackFn EventCallback;

		RendererSpecification RendererSpec;
		SoundPlayerSpecification SoundPlayerSpec;
	};

	class Window
	{
	public:
		Window(const WindowSpecification& windowSpec = WindowSpecification());
		~Window();

		void Create();
		void Destroy();

		void Update();

		void PollEvents();

		const Vec2<uint32_t> GetSize() const;
		bool ShouldClose() const { return m_ShouldClose; }

		Renderer& GetRenderer() { return m_Renderer; }
		SoundPlayer& GetSoundPlayer() { return m_SoundPlayer; }

	private:

		void RaiseEvent(Event& event);

	private:
		WindowSpecification m_Specification;

		SDL_Window* m_SDLWindow = nullptr;
		
		Renderer m_Renderer;
		SoundPlayer m_SoundPlayer;

		bool m_ShouldClose = false;
	};

}