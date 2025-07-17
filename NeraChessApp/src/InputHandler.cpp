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
			continue;

		if (event.type == SDL_QUIT)
		{
			m_InputEvents.emplace_back(EventTypeQuit);
		}
		else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
		{
			m_InputEvents.emplace_back(EventTypeWindowResize);
		}

    }
}

int InputHandler::PollInputEvent(InputEvent* event)
{	
	if (!m_InputEvents.empty())
	{
		*event = m_InputEvents.back();
		m_InputEvents.pop_back();
		return 1;
	}
	return 0;
}

bool InputHandler::IsImGuiWantCapture() const
{
	if (m_IO)
		return m_IO->WantCaptureKeyboard || m_IO->WantCaptureMouse;
	else
		return false;
}