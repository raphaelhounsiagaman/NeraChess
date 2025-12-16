#include "SoundPlayer.h"

#include "SDL_mixer.h"

#include <assert.h>
#include <print>

namespace NeraCore
{

	void SoundPlayer::PlaySound(const Sound& sound, int loops)
	{
		Mix_PlayChannel(-1, sound.m_Chunk, loops);
	}

	SoundPlayer::~SoundPlayer()
	{
		Destroy();
	}

	void SoundPlayer::Init(const SoundPlayerSpecification& soundPlayerSpecs)
	{
		if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, m_AudioBuffers))
		{
			std::println("Error: Mix_OpenAudio(): {}", SDL_GetError());
			assert(false);
			return;
		}
		Mix_QuerySpec(&m_AudioRate, &m_AudioFormat, &m_AudioChannels);
		m_Bits = m_AudioFormat & 0xFF;
		Mix_AllocateChannels(c_ChannelCount);
	}

	void SoundPlayer::Destroy()
	{
		Mix_CloseAudio();
	}

}
