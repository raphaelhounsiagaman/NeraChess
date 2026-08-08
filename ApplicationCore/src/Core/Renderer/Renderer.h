#pragma once

#include "Color.h"
#include "Core/Math/Vec2.h"
#include "SDL.h"
#include "Sprite.h"
#include "Texture.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace ApplicationCore
{
struct RendererSpecification
{
	SDL_Window* Window = nullptr;
	bool VSync = true;
};

class Renderer
{
  public:
	void Clear(Color color);
	void DrawSquare(Vec2<uint32_t> position, Vec2<uint32_t> size, Color color);
	void DrawSprite(const Sprite& sprite, Vec2<uint32_t> position, Vec2<uint32_t> size);

	ImGuiIO* GetIO() const
	{
		return m_IO;
	}
	SDL_Renderer* GetSDLRenderer() const
	{
		return m_SDLRenderer;
	}

  private:
	Renderer() = default;
	~Renderer();

	void Init(const RendererSpecification& rendererSpec = RendererSpecification());
	void Destroy();

	void Present();

	SDL_Window* m_SDLWindow = nullptr;
	SDL_Renderer* m_SDLRenderer = nullptr;

	ImGuiIO* m_IO = nullptr;
	friend class Window;
};
} // namespace ApplicationCore
