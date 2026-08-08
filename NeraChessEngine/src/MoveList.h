#pragma once

#include "Move.h"

#include <array>
#include <cassert>

namespace NeraChessEngine
{

template <size_t maxMoves> struct MoveList
{
  public:
	MoveList() = default;
	MoveList(Move move)
	{
		push(move);
	}

	void push(Move move)
	{
		assert(m_MoveCount < m_Moves.size());
		m_Moves[m_MoveCount++] = move;
	}

	Move& operator[](size_t i)
	{
		assert(i < m_MoveCount);
		return m_Moves[i];
	}

	const Move& operator[](size_t i) const
	{
		assert(i < m_MoveCount);
		return m_Moves[i];
	}

	[[nodiscard]] size_t size() const
	{
		return m_MoveCount;
	}

	[[nodiscard]] bool empty() const
	{
		return m_MoveCount == 0;
	}

	void clear()
	{
		m_MoveCount = 0;
	}

	// Range-for support
	inline Move* begin() noexcept
	{
		return m_Moves.data();
	}
	inline Move* end() noexcept
	{
		return m_Moves.data() + m_MoveCount;
	}
	inline const Move* begin() const noexcept
	{
		return m_Moves.data();
	}
	inline const Move* end() const noexcept
	{
		return m_Moves.data() + m_MoveCount;
	}

  private:
	std::array<Move, maxMoves> m_Moves{};
	size_t m_MoveCount = 0;
};

} // namespace NeraChessEngine
