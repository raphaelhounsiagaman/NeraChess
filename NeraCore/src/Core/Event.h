#pragma once

#include <string>
#include <functional>

namespace NeraCore
{

	enum class EventType
	{
		None = 0,
		WindowClose, WindowResize,
		KeyPressed, KeyReleased,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled,
	};

#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

	class Event
	{
	public:
		bool Handled = false;

		virtual ~Event() {}
		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual std::string ToString() const { return GetName(); }
	};

	class EventDispatcher
	{
		template<typename TEvent>
			requires std::is_base_of_v<Event, TEvent>
		using EventFn = std::function<bool(TEvent&)>;
	public:
		EventDispatcher(Event& event)
			: m_Event(event) {
		}

		template<typename TEvent>
			requires std::is_base_of_v<Event, TEvent>
		bool Dispatch(EventFn<TEvent> func)
		{
			if (m_Event.GetEventType() == TEvent::GetStaticType() && !m_Event.Handled)
			{
				m_Event.Handled = func(*(TEvent*)&m_Event);
				return true;
			}
			return false;
		}
	private:
		Event& m_Event;
	};


}
