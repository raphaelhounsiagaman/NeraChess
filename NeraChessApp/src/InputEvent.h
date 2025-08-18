#pragma once

#include <cstdint>

#include "InputEventType.h"

struct Coord
{
	int x = 0;
	int y = 0;
};

struct InputEvent
{
	uint16_t type = EventTypeNone;

	Coord eventPos{ 0, 0 };

};