#include "Renderer.h"

#include <print>
#include <assert.h>

namespace NeraCore
{

	Renderer::~Renderer()
	{
		Destroy();
	}

	void Renderer::Init(const RendererSpecification& rendererSpec)
	{
		m_SDLWindow = rendererSpec.Window;

		if (IMG_Init(IMG_INIT_PNG) == 0)
		{
			std::println("Error: IMG_Init(): {}", SDL_GetError());
			assert(false);
			return;
		}

		uint32_t rendererFlags = SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED;

		m_SDLRenderer = SDL_CreateRenderer(m_SDLWindow, -1, rendererFlags);
		if (m_SDLRenderer == nullptr)
		{
			std::println("Error: SDL_CreateRenderer(): {}", SDL_GetError());
			assert(false);
			return;
		}
		SDL_SetRenderDrawBlendMode(m_SDLRenderer, SDL_BLENDMODE_BLEND);

		//Initialize Dear ImGui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		m_IO = &ImGui::GetIO();
		m_Style = &ImGui::GetStyle();

		ImGui_ImplSDL2_InitForSDLRenderer(m_SDLWindow, m_SDLRenderer);
		ImGui_ImplSDLRenderer2_Init(m_SDLRenderer);

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
	}
	
	void Renderer::Destroy()
	{
		if (m_SDLRenderer)
		{
			IMG_Quit();

			ImGui_ImplSDLRenderer2_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();

			SDL_DestroyRenderer(m_SDLRenderer);
			m_SDLRenderer = nullptr;
		}
	}

	void Renderer::Present()
	{
		// ImGUI Rendering
		ImGui::Render();
		SDL_RenderSetScale(
			m_SDLRenderer,
			m_IO->DisplayFramebufferScale.x,
			m_IO->DisplayFramebufferScale.y
		);
		ImGui_ImplSDLRenderer2_RenderDrawData(
			ImGui::GetDrawData(),
			m_SDLRenderer);

		// Present
		SDL_RenderPresent(m_SDLRenderer);

		// Create new frame
		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
	}

	void Renderer::Clear(Color color)
	{
		SDL_SetRenderDrawColor(m_SDLRenderer, color.R, color.G, color.B, color.A);
		SDL_RenderClear(m_SDLRenderer);
	}

	void Renderer::DrawSquare(Vec2<uint32_t> position, Vec2<uint32_t> size, Color color)
	{
		SDL_SetRenderDrawColor(m_SDLRenderer, color.R, color.G, color.B, color.A);
		SDL_Rect rect = { (int)position.X, (int)position.Y, (int)size.X, (int)size.Y };
		SDL_RenderFillRect(m_SDLRenderer, &rect);
	}

	void Renderer::DrawSprite(Sprite sprite, Vec2<uint32_t> position, Vec2<uint32_t> size)
	{
		SDL_Rect source = { (int)sprite.Position.X, (int)sprite.Position.Y, (int)sprite.Size.X, (int)sprite.Size.Y };
		SDL_Rect dest = { (int)position.X, (int)position.Y, (int)size.X, (int)size.Y };

		SDL_RenderCopy(m_SDLRenderer, sprite.Texture.GetSDLTexture(), &source, &dest);
	}

}
