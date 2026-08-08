#include "Texture.h"

#include "Core/Application.h"

#include <stdexcept>
#include <string>

namespace ApplicationCore
{
Texture::Texture(const std::string& fileName)
{

	Renderer& renderer = Application::Get().GetWindow()->GetRenderer();
	m_SDL_Texture = IMG_LoadTexture(renderer.GetSDLRenderer(), fileName.c_str());

	if (m_SDL_Texture == nullptr)
	{
		throw std::runtime_error(std::string("Texture loading failed for '") + fileName + "': " + IMG_GetError());
	}

	SDL_QueryTexture(m_SDL_Texture, nullptr, nullptr, &m_Size.X, &m_Size.Y);
}

Texture::~Texture()
{
	if (m_SDL_Texture)
	{
		SDL_DestroyTexture(m_SDL_Texture);
		m_SDL_Texture = nullptr;
	}
}

} // namespace ApplicationCore
