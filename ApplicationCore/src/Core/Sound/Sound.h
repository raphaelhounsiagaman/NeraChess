#pragma once

#include "SDL_mixer.h"

#include <string>

namespace ApplicationCore
{

class Sound
{
  public:
	explicit Sound(const std::string& soundPath);
	~Sound();

	Sound(const Sound&) = delete;
	Sound& operator=(const Sound&) = delete;

	Sound(Sound&& other) noexcept;
	Sound& operator=(Sound&& other) noexcept;

  private:
	friend class SoundPlayer;

	Mix_Chunk* m_Chunk = nullptr;
};

} // namespace ApplicationCore
