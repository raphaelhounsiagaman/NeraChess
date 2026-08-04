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

	uint16_t RepetitionTable::GetRepetitionCount(uint64_t positionKey,
		std::size_t reversiblePlies) const
	{
		uint16_t count = 0;
		for (std::size_t distance = 0;
			distance < m_Keys.size() && distance <= reversiblePlies && count < 3;
			distance += 2)
		{
			count += m_Keys[m_Keys.size() - 1 - distance] == positionKey;
		}
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
