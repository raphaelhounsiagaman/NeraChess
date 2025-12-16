#include "Sound.h"

#include <print>
#include <assert.h>

namespace NeraCore
{

	Sound::Sound(const std::string& soundPath)
	{
		m_Chunk = Mix_LoadWAV(soundPath.c_str());
		if (!m_Chunk)
		{
			std::println("Error: Mix_LoadWAV(): {}", SDL_GetError());
			assert(false);
			return;
		}

	}

	Sound::~Sound()
	{
		if (m_Chunk) 
		{
			Mix_FreeChunk(m_Chunk);
		}
		m_Chunk = nullptr;
	}


	Sound::Sound(Sound&& other) noexcept
	{
		m_Chunk = other.m_Chunk;
		other.m_Chunk = nullptr;
	}

	Sound& Sound::operator=(Sound&& other) noexcept
	{
		if (this != &other) 
		{
			if (m_Chunk)
				Mix_FreeChunk(m_Chunk);
			m_Chunk = other.m_Chunk;
			other.m_Chunk = nullptr;
		}
		return *this;
	}

}