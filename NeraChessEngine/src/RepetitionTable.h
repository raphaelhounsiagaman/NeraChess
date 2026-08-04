#pragma once

#include <cassert>
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

            uint16_t GetRepetitionCount(uint64_t positionKey) const;

		void Clear();

		bool operator==(const RepetitionTable& other) const;

	private:

			std::vector<uint64_t> m_Keys;

	};

} // namespace NeraChessEngine
