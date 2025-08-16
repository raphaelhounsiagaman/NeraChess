#pragma once

#include <cstdint>

using Bitboard = uint64_t;

namespace BitUtil
{

    uint8_t TrailingZeroCount(uint64_t value);
    
    bool IsPow2(uint64_t value);

    uint8_t PopLSB(uint64_t& value);
    uint8_t GetLSBIndex(uint64_t value);

    uint8_t PopCnt(uint64_t value);

    uint64_t Shift(uint64_t value, int numSquaresToShift);

}