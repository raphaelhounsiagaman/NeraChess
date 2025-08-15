#pragma once

#include <cstdint>

#include "InputEventType.h"

struct coord
{
	int x = 0;
	int y = 0;
};

struct InputEvent
{
	uint16_t type = EventTypeNone;

	coord eventPos{ 0, 0 };

};