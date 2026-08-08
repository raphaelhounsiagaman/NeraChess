#include "SoundPlayer.h"

#include "SDL_mixer.h"

#include <stdexcept>
#include <string>

namespace ApplicationCore
{

void SoundPlayer::PlaySound(const Sound& sound, int loops)
{
	Mix_PlayChannel(-1, sound.m_Chunk, loops);
}

SoundPlayer::~SoundPlayer()
{
	Destroy();
}

void SoundPlayer::Init(const SoundPlayerSpecification&)
{
	if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, m_AudioBuffers))
	{
		throw std::runtime_error(std::string("Audio initialization failed: ") + Mix_GetError());
	}
	Mix_QuerySpec(&m_AudioRate, &m_AudioFormat, &m_AudioChannels);
	m_Bits = m_AudioFormat & 0xFF;
	Mix_AllocateChannels(c_ChannelCount);
	m_Initialized = true;
}

void SoundPlayer::Destroy()
{
	if (m_Initialized)
	{
		Mix_CloseAudio();
		m_Initialized = false;
	}
}

} // namespace ApplicationCore
