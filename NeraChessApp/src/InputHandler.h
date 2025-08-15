#pragma once

#include <vector>

#include "imgui.h"
#include "SDL.h"

#include "InputEvent.h"

class InputHandler
{
public:
	InputHandler();
	~InputHandler() = default;
	
	void Process();

	int PollInputEvent(InputEvent& event);

	void SetImGuiIO(const ImGuiIO* io) { m_IO = io; }

	void AddInputEvent(InputEvent event);

private:

	bool IsImGuiWantCapture() const;
	
private:

	std::vector<InputEvent> m_InputEvents{};

	const ImGuiIO* m_IO = nullptr;

};