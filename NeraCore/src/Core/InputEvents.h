#pragma once

#include "Core/Math/Vec2.h"

#include "Event.h"

#include <format>

namespace NeraCore
{

	//
	// Key Events
	//

	class KeyEvent : public Event
	{
	public:
		inline int GetKeyCode() const { return m_KeyCode; }
	protected:
		KeyEvent(int keycode)
			: m_KeyCode(keycode) {}

		int m_KeyCode;
	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(int keycode, bool isRepeat)
			: KeyEvent(keycode), m_IsRepeat(isRepeat) {}

		inline bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override
		{
			return std::format("KeyPressedEvent: {} (repeat={})", m_KeyCode, m_IsRepeat);
		}

		EVENT_CLASS_TYPE(KeyPressed)
	private:
		bool m_IsRepeat;
	};

	class KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(int keycode)
			: KeyEvent(keycode) {
		}

		std::string ToString() const override
		{
			return std::format("KeyReleasedEvent: {}", m_KeyCode);
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};

	//
	// Mouse Events
	//

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(uint32_t x, uint32_t y)
			: m_MousePosition(x, y) {
		}

		inline Vec2<uint32_t> GetPosition() const { return m_MousePosition; }
		inline uint32_t GetX() const { return m_MousePosition.X; }
		inline uint32_t GetY() const { return m_MousePosition.Y; }

		std::string ToString() const override
		{
			return std::format("MouseMovedEvent: {}, {}", m_MousePosition.X, m_MousePosition.Y);
		}

		EVENT_CLASS_TYPE(MouseMoved)
	private:
		Vec2<uint32_t> m_MousePosition;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(int xOffset, int yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {
		}

		inline int GetXOffset() const { return m_XOffset; }
		inline int GetYOffset() const { return m_YOffset; }

		std::string ToString() const override
		{
			return std::format("MouseScrolledEvent: {}, {}", m_XOffset, m_YOffset);
		}

		EVENT_CLASS_TYPE(MouseScrolled)
	private:
		int m_XOffset, m_YOffset;
	};

	class MouseButtonEvent : public Event
	{
	public:
		inline int GetMouseButton() const { return m_Button; }
	protected:
		MouseButtonEvent(int button)
			: m_Button(button) {
		}

		int m_Button;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonPressedEvent(int button)
			: MouseButtonEvent(button) {
		}

		std::string ToString() const override
		{
			return std::format("MouseButtonPressedEvent: {}", m_Button);
		}

		EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonReleasedEvent(int button)
			: MouseButtonEvent(button) {
		}

		std::string ToString() const override
		{
			return std::format("MouseButtonReleasedEvent: {}", m_Button);
		}

		EVENT_CLASS_TYPE(MouseButtonReleased)
	};





}
