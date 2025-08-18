#include "Util.h"

#include <bit>

namespace BitUtil
{
	uint8_t TrailingZeroCount(uint64_t value)
	{
		return std::countr_zero(value);
	}

	bool IsPow2(uint64_t value)
	{
		return (value & (value - 1)) == 0 && value > 0;
	}

	uint8_t PopLSB(uint64_t& value)
	{
		uint8_t i = TrailingZeroCount(value);
		value &= (value - 1);
		return i;
	}

	uint8_t GetLSBIndex(uint64_t value)
	{
		return TrailingZeroCount(value);
	}

	uint8_t PopCnt(uint64_t value)
	{
		return std::popcount(value);
	}

	uint64_t Shift(uint64_t value, int numSquaresToShift)
	{
		return (numSquaresToShift >= 0) ? (value << numSquaresToShift)
			: (value >> -numSquaresToShift);

	}

}


