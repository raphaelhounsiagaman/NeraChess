#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace NeraChessEngine
{

    class RepetitionTable
	{
	public:
			RepetitionTable();
		~RepetitionTable() = default;

            void AddEntry(uint64_t positionKey);
            void RemoveEntry(uint64_t positionKey);

            uint16_t GetRepetitionCount(uint64_t positionKey, std::size_t reversiblePlies) const;

			// Number of positions recorded: the game history the table was built from,
			// plus every move made on it since. Search uses this to measure distance
			// back to its root without relying on ply counters, which a null move
			// leaves unadvanced.
			std::size_t Size() const { return m_Keys.size(); }

		void Clear();

		bool operator==(const RepetitionTable& other) const;

	private:

			std::vector<uint64_t> m_Keys;

	};

} // namespace NeraChessEngine
