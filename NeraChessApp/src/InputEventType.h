#pragma once

#include <cstdint>

enum InputEventType : uint16_t
{
	EventTypeNone = 0,

	EventTypeQuit,

	EventTypeWindowResize,

	EventTypeKeyPressed,

};