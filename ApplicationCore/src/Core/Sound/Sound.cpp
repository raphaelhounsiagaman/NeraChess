#include "Sound.h"

#include <stdexcept>
#include <string>

namespace ApplicationCore
{

Sound::Sound(const std::string& soundPath)
{
	m_Chunk = Mix_LoadWAV(soundPath.c_str());
	if (!m_Chunk)
	{
		throw std::runtime_error(std::string("Sound loading failed for '") + soundPath + "': " + Mix_GetError());
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

} // namespace ApplicationCore
