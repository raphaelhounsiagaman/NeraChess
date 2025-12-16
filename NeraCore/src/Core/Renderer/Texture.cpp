#include "Texture.h"

#include "Core/Application.h"

#include <print>

namespace NeraCore
{
	Texture::Texture(const std::string& fileName)
	{

		Renderer& renderer = Application::Get().GetWindow()->GetRenderer();
		m_SDL_Texture = IMG_LoadTexture(renderer.GetSDLRenderer(), fileName.c_str());

		if (m_SDL_Texture == nullptr)
		{
			std::println("Error: IMG_LoadTexture(): {}", SDL_GetError());
			assert(false);
			return;
		}

		SDL_QueryTexture(m_SDL_Texture, nullptr, nullptr, &m_Size.X, &m_Size.Y);
	}

	Texture::~Texture()
	{
		// TODO: delete the texture if necessary
	}


} // namespace NeraCore