#pragma once

#include "Core/Math/Vec2.h"
#include "Texture.h"

namespace NeraCore
{
	struct Sprite
	{
		Texture& Texture;
		Vec2<uint32_t> Position{ 0, 0 };
		Vec2<uint32_t> Size{ 0, 0 };
	};

} // namespace NeraCore