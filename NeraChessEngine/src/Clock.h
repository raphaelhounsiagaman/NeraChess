#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>

namespace NeraChessEngine
{

	class Clock
	{
	public:
		using Duration = std::chrono::milliseconds;

		explicit Clock(Duration initialTime = std::chrono::minutes(5),
			Duration increment = Duration{ 0 })
			: m_InitialTime(std::max(Duration{ 0 }, initialTime)),
			  m_Increment(std::max(Duration{ 0 }, increment)),
			  m_WhiteRemaining(m_InitialTime),
			  m_BlackRemaining(m_InitialTime)
		{
		}
		~Clock() = default;

	public:
		void SetTimeControl(Duration initialTime, Duration increment = Duration{ 0 })
		{
			std::scoped_lock lock(m_Mutex);
			m_InitialTime = std::max(Duration{ 0 }, initialTime);
			m_Increment = std::max(Duration{ 0 }, increment);
			m_WhiteRemaining = m_InitialTime;
			m_BlackRemaining = m_InitialTime;
			m_Running = false;
			m_Paused = false;
			m_WhiteActive = true;
		}

		void Start()
		{
			std::scoped_lock lock(m_Mutex);
			m_WhiteRemaining = m_InitialTime;
			m_BlackRemaining = m_InitialTime;
			m_WhiteActive = true;
			m_Running = true;
			m_Paused = false;
			m_LastUpdate = std::chrono::steady_clock::now();
		}

		void Press()
		{
			std::scoped_lock lock(m_Mutex);
			if (!m_Running)
				return;
			const auto now = std::chrono::steady_clock::now();
			ConsumeElapsed(now);
			Duration& active = m_WhiteActive ? m_WhiteRemaining : m_BlackRemaining;
			active += m_Increment;
			m_WhiteActive = !m_WhiteActive;
			m_LastUpdate = now;
		}

		void Stop()
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Running)
				ConsumeElapsed(std::chrono::steady_clock::now());
			m_Running = false;
			m_Paused = false;
		}

		void Pause()
		{
			std::scoped_lock lock(m_Mutex);
			if (!m_Running || m_Paused)
				return;
			ConsumeElapsed(std::chrono::steady_clock::now());
			m_Paused = true;
		}

		void Resume()
		{
			std::scoped_lock lock(m_Mutex);
			if (!m_Running || !m_Paused)
				return;
			m_LastUpdate = std::chrono::steady_clock::now();
			m_Paused = false;
		}

		Duration GetRemaining(bool white) const
		{
			std::scoped_lock lock(m_Mutex);
			Duration remaining = white ? m_WhiteRemaining : m_BlackRemaining;
			if (m_Running && !m_Paused && white == m_WhiteActive)
			{
				remaining -= std::chrono::duration_cast<Duration>(
					std::chrono::steady_clock::now() - m_LastUpdate);
			}
			return std::max(Duration{ 0 }, remaining);
		}

		Duration GetIncrement() const
		{
			std::scoped_lock lock(m_Mutex);
			return m_Increment;
		}

		bool IsWhiteActive() const
		{
			std::scoped_lock lock(m_Mutex);
			return m_WhiteActive;
		}

	private:
		void ConsumeElapsed(std::chrono::steady_clock::time_point now)
		{
			if (!m_Running || m_Paused)
				return;
			Duration& active = m_WhiteActive ? m_WhiteRemaining : m_BlackRemaining;
			active = std::max(Duration{ 0 }, active -
				std::chrono::duration_cast<Duration>(now - m_LastUpdate));
			m_LastUpdate = now;
		}

	private:
		mutable std::mutex m_Mutex;
		Duration m_InitialTime;
		Duration m_Increment;
		Duration m_WhiteRemaining;
		Duration m_BlackRemaining;
		std::chrono::steady_clock::time_point m_LastUpdate{};
		bool m_WhiteActive = true;
		bool m_Running = false;
		bool m_Paused = false;
	};
}
