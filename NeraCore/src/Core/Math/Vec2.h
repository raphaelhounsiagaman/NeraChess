#pragma once

#include <cstdint>

namespace NeraCore
{
	template<typename T>
	struct Vec2
	{
		T X = 0;
		T Y = 0;

		Vec2 operator+(const Vec2& other) const
		{
			return Vec2{ X + other.X, Y + other.Y };
		}

		Vec2 operator-(const Vec2& other) const
		{
			return Vec2{ X - other.X, Y - other.Y };
		}

		Vec2 operator*(const T scalar) const
		{
			return Vec2{ X * scalar, Y * scalar };
		}

		Vec2 operator/(const T scalar) const
		{
			return Vec2{ X / scalar, Y / scalar };
		}

	};

}
