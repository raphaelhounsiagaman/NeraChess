#include "RepetitionTable.h"

namespace NeraChessEngine
{
	RepetitionTable::RepetitionTable()
	{
		m_Keys.reserve(256);
	}

	void RepetitionTable::AddEntry(uint64_t positionKey)
	{
		m_Keys.push_back(positionKey);
	}

	void RepetitionTable::RemoveEntry(uint64_t positionKey)
	{
		assert(!m_Keys.empty());
		assert(m_Keys.back() == positionKey);
		m_Keys.pop_back();
	}

	uint16_t RepetitionTable::GetRepetitionCount(uint64_t positionKey) const
	{
		uint16_t count = 0;
		for (auto it = m_Keys.rbegin(); it != m_Keys.rend() && count < 3; ++it)
			count += *it == positionKey;
		return count;
	}

	void RepetitionTable::Clear()
	{
		m_Keys.clear();
	}

	bool RepetitionTable::operator==(const RepetitionTable& other) const
	{
		return m_Keys == other.m_Keys;
	}

} // namespace NeraChessEngine
