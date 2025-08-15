#pragma once

#include <array>
#include <cassert>

#include "Move.h"

struct MoveList
{
public:
	
    inline void push(Move m) noexcept {
        assert(m_MoveCount < m_Moves.size()); // debug safety
        m_Moves[m_MoveCount++] = m;
    }

    inline void pop() noexcept {
        assert(m_MoveCount > 0); // debug safety
        --m_MoveCount;
	}

    inline Move& operator[](uint16_t i) noexcept {
        return m_Moves[i];
    }

    inline const Move& operator[](uint16_t i) const noexcept {
        return m_Moves[i];
    }

    inline uint16_t size() const noexcept {
        return m_MoveCount;
    }

    inline void clear() noexcept {
        m_MoveCount = 0;
    }

    // Range-for support
    inline Move* begin() noexcept { return m_Moves.data(); }
    inline Move* end() noexcept { return m_Moves.data() + m_MoveCount; }
    inline const Move* begin() const noexcept { return m_Moves.data(); }
    inline const Move* end() const noexcept { return m_Moves.data() + m_MoveCount; }

private:

	std::array<Move, 218> m_Moves;
	uint8_t m_MoveCount = 0;

};
