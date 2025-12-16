#pragma once

#include "Sound.h"

namespace NeraCore
{

	struct SoundPlayerSpecification
	{

	};

	class SoundPlayer
	{
	public:
		void PlaySound(const Sound& sound, int loops = 0);

	private:
		SoundPlayer() = default;
		~SoundPlayer();

		void Init(const SoundPlayerSpecification& soundPlayerSpecs = SoundPlayerSpecification());
		void Destroy();

	private:

		static constexpr int c_ChannelCount = 32;

		int m_AudioBuffers = 2048;
		int m_AudioRate{};
		int m_AudioChannels{};

		uint16_t m_AudioFormat{};
		int m_Bits = 0;




		friend class Window;
	};



}