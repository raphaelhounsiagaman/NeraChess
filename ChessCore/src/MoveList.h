#pragma once

#include <array>
#include <cassert>

#include "Move.h"

namespace ChessCore
{

    template<size_t maxMoves>
    struct MoveList
    {
    public:
	
        MoveList() = default;
        MoveList(Move move)
        {
            push(move);
        }

        MoveList operator+(MoveList& other)
        {
            MoveList<maxMoves> result;
            for (size_t i = 0; i < this->size(); i++)
                result.push((*this)[i]);
            for (size_t i = 0; i < other.size(); i++)
                result.push(other[i]);
		    return result;
        }

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

        Move& operator[](size_t i)
        {
            return m_Moves[i];
        }

        const Move& operator[](size_t i) const
        {
            return m_Moves[i];
        }

        size_t size() const
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

        std::array<Move, maxMoves> m_Moves{};
	    size_t m_MoveCount = 0;

    };

} // namespace ChesCore