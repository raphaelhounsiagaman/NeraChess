#pragma once

#include "Event.h"
#include "Math/Vec2.h"

#include <format>

namespace NeraCore
{
	class WindowClosedEvent : public Event
	{
	public:
		WindowClosedEvent() {}

		EVENT_CLASS_TYPE(WindowClose)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height)
			: m_NewSize(width, height) {}

		inline Vec2<uint32_t> GetNewSize() const { return m_NewSize; }
		inline uint32_t GetWidth() const { return m_NewSize.X; }
		inline uint32_t GetHeight() const { return m_NewSize.Y; }

		std::string ToString() const override
		{
			return std::format("WindowResizeEvent: {}, {}", m_NewSize.X, m_NewSize.Y);
		}

		EVENT_CLASS_TYPE(WindowResize)
	private:
		Vec2<uint32_t> m_NewSize;
	};
}
