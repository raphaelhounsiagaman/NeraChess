#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string_view>

namespace NeraChessEngine
{
	using ClockDuration = std::chrono::milliseconds;

	struct TimeControl
	{
		ClockDuration initialTime{ std::chrono::minutes(5) };
		ClockDuration increment{ 0 };

		constexpr bool operator==(const TimeControl&) const = default;
	};

	struct TimeControlPreset
	{
		std::string_view label;
		TimeControl timeControl;
	};

	inline constexpr std::array<TimeControlPreset, 5> MainTimeControls
	{
		TimeControlPreset{ "1 min", { std::chrono::minutes(1), ClockDuration{ 0 } } },
		TimeControlPreset{ "3 min + 2 sec", { std::chrono::minutes(3), std::chrono::seconds(2) } },
		TimeControlPreset{ "10 min", { std::chrono::minutes(10), ClockDuration{ 0 } } },
		TimeControlPreset{ "15 min + 10 sec", { std::chrono::minutes(15), std::chrono::seconds(10) } },
		TimeControlPreset{ "90 min + 40 sec", { std::chrono::minutes(90), std::chrono::seconds(40) } },
	};

	struct ClockSnapshot
	{
		ClockDuration whiteRemaining{ 0 };
		ClockDuration blackRemaining{ 0 };
		ClockDuration increment{ 0 };
		bool whiteActive = true;
		bool running = false;
		bool paused = false;
	};

	class Clock
	{
	public:
		using Duration = ClockDuration;

		explicit Clock(Duration initialTime = std::chrono::minutes(5),
			Duration increment = Duration{ 0 })
			: Clock(TimeControl{ initialTime, increment })
		{
		}

		explicit Clock(TimeControl timeControl)
			: m_InitialTime(std::max(Duration{ 0 }, timeControl.initialTime)),
			  m_Increment(std::max(Duration{ 0 }, timeControl.increment)),
			  m_WhiteRemaining(m_InitialTime),
			  m_BlackRemaining(m_InitialTime)
		{
		}
		~Clock() = default;

	public:
		void SetTimeControl(Duration initialTime, Duration increment = Duration{ 0 })
		{
			SetTimeControl(TimeControl{ initialTime, increment });
		}

		void SetTimeControl(TimeControl timeControl)
		{
			std::scoped_lock lock(m_Mutex);
			m_InitialTime = std::max(Duration{ 0 }, timeControl.initialTime);
			m_Increment = std::max(Duration{ 0 }, timeControl.increment);
			m_WhiteRemaining = m_InitialTime;
			m_BlackRemaining = m_InitialTime;
			m_Running = false;
			m_Paused = false;
			m_WhiteActive = true;
		}

		void Start(bool whiteToMove = true)
		{
			std::scoped_lock lock(m_Mutex);
			m_WhiteRemaining = m_InitialTime;
			m_BlackRemaining = m_InitialTime;
			m_WhiteActive = whiteToMove;
			m_Running = true;
			m_Paused = false;
			m_LastUpdate = std::chrono::steady_clock::now();
		}

		bool Press()
		{
			std::scoped_lock lock(m_Mutex);
			if (!m_Running)
				return false;
			const auto now = std::chrono::steady_clock::now();
			ConsumeElapsed(now);
			Duration& active = m_WhiteActive ? m_WhiteRemaining : m_BlackRemaining;
			if (active.count() == 0)
				return false;
			active += m_Increment;
			m_WhiteActive = !m_WhiteActive;
			m_LastUpdate = now;
			return true;
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
			const ClockSnapshot snapshot = GetSnapshot();
			return white ? snapshot.whiteRemaining : snapshot.blackRemaining;
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

		TimeControl GetTimeControl() const
		{
			std::scoped_lock lock(m_Mutex);
			return { m_InitialTime, m_Increment };
		}

		ClockSnapshot GetSnapshot() const
		{
			std::scoped_lock lock(m_Mutex);
			ClockSnapshot snapshot
			{
				.whiteRemaining = m_WhiteRemaining,
				.blackRemaining = m_BlackRemaining,
				.increment = m_Increment,
				.whiteActive = m_WhiteActive,
				.running = m_Running,
				.paused = m_Paused,
			};

			if (m_Running && !m_Paused)
			{
				Duration& active = m_WhiteActive
					? snapshot.whiteRemaining
					: snapshot.blackRemaining;
				active = std::max(Duration{ 0 }, active -
					std::chrono::duration_cast<Duration>(
						std::chrono::steady_clock::now() - m_LastUpdate));
			}
			return snapshot;
		}

		bool HasExpired(bool white) const
		{
			return GetRemaining(white).count() == 0;
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
