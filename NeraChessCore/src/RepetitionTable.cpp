#include "RepetitionTable.h"

void RepetitionTable::AddEntry(const std::array<Bitboard, 12>& pieceBitboards)
{
	for (RepetitionEntry& entry : m_Entries)
	{
		if (entry.pieceBitboards == pieceBitboards)
		{
			entry.repetitionCount += 1;
			return;
		}
	}

	for (RepetitionEntry& entry : m_Entries)
	{
		if (entry.repetitionCount == 0)
		{
			entry.pieceBitboards = pieceBitboards;
			entry.repetitionCount += 1;
			return;
		}
	}
}

void RepetitionTable::RemoveEntry(const std::array<Bitboard, 12>& pieceBitboards)
{
	for (RepetitionEntry& entry : m_Entries)
	{
		if (entry.pieceBitboards == pieceBitboards)
		{
			entry.repetitionCount = std::max(0, entry.repetitionCount - 1);
			return;
		}
	}
}

uint8_t RepetitionTable::GetRepetitionCount(const std::array<Bitboard, 12>& pieceBitboards) const
{
	for (const RepetitionEntry& entry : m_Entries)
	{
		if (entry.pieceBitboards == pieceBitboards)
		{
			return entry.repetitionCount;
		}
	}

	return 0;
}

void RepetitionTable::Clear()
{
	for (RepetitionEntry& entry : m_Entries)
	{
		entry.repetitionCount = 0;
	}
}

bool RepetitionTable::operator==(const RepetitionTable& other) const
{
	bool same = true;
	
	for (uint8_t i = 0; i < m_Entries.size(); i++)
	{
		if (m_Entries[i].repetitionCount != other.m_Entries[i].repetitionCount)
		{
			same = false;
			break;
		}
		else if (m_Entries[i].repetitionCount != 0 && m_Entries[i].pieceBitboards != other.m_Entries[i].pieceBitboards)
		{
			same = false;
			break;
		}
	}

	return same;
}
