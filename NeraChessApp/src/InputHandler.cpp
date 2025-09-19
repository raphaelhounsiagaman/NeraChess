#include "InputHandler.h"

#include <iostream>
#include <vector>

#include "SDL.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include "InputEvent.h"

InputHandler::InputHandler()
{
	m_InputEvents.reserve(32);
}

void InputHandler::Process()
{
	SDL_Event event;
    while (SDL_PollEvent(&event))
    {
		ImGui_ImplSDL2_ProcessEvent(&event);
		if (IsImGuiWantCapture())
		{
			//continue;
		}

		if (event.type == SDL_QUIT)
		{
			m_InputEvents.emplace_back(EventTypeQuit);
		}
		else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
		{
			m_InputEvents.emplace_back(EventTypeWindowResize);
		}
		else if (event.type == SDL_KEYDOWN)
		{
			m_InputEvents.emplace_back(EventTypeKeyPressed);
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN)
		{
			if (event.button.button == SDL_BUTTON_LEFT)
			{
				InputEvent inputEvent;
				inputEvent.type = EventTypeLMBPressed;
				inputEvent.eventPos = { event.button.x, event.button.y };
				m_InputEvents.push_back(inputEvent);
			}
			if (event.button.button == SDL_BUTTON_RIGHT)
			{
				InputEvent inputEvent;
				inputEvent.type = EventTypeRMBPressed;
				inputEvent.eventPos = { event.button.x, event.button.y };
				m_InputEvents.push_back(inputEvent);
			}
		}

    }
}

int InputHandler::PollInputEvent(InputEvent& event)
{	
	if (!m_InputEvents.empty())
	{
		event = m_InputEvents.back();
		m_InputEvents.pop_back();
		return 1;
	}
	return 0;
}

void InputHandler::AddInputEvent(InputEvent inputEvent)
{
	m_InputEvents.push_back(inputEvent);
}

bool InputHandler::IsImGuiWantCapture() const
{
	if (m_IO)
		return m_IO->WantCaptureKeyboard || m_IO->WantCaptureMouse;
	else
		return false;
}