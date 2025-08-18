#pragma once

#include <array>
#include <cassert>

#include "Move.h"

struct MoveList
{
public:
	
    void push(Move move) 
    {
        assert(m_MoveCount < m_Moves.size());
        m_Moves[m_MoveCount++] = move;
    }

    void pop() 
    {
        assert(m_MoveCount > 0);
        --m_MoveCount;
	}

    Move& operator[](uint8_t i) 
    {
        return m_Moves[i];
    }

    const Move& operator[](uint8_t i) const 
    {
        return m_Moves[i];
    }

    uint8_t size() const 
    {
        return m_MoveCount;
    }

    void clear()
    {
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
