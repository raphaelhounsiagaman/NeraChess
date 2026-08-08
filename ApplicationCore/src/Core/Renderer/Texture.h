#pragma once

#include "Core/Math/Vec2.h"
#include "SDL.h"
#include "SDL_image.h"

#include <string>

namespace ApplicationCore
{
class Texture
{
  public:
	explicit Texture(const std::string& fileName);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	SDL_Texture* GetSDLTexture() const
	{
		return m_SDL_Texture;
	}
	Vec2<int> GetSize() const
	{
		return m_Size;
	}

  private:
	SDL_Texture* m_SDL_Texture = nullptr;
	Vec2<int> m_Size{0, 0};
};

} // namespace ApplicationCore
