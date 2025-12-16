#pragma once

#include <array>

namespace ChessCore
{

	using Bitboard = uint64_t;

	struct RepetitionEntry
	{
		std::array<Bitboard, 12> pieceBitboards;
		uint8_t repetitionCount = 0;
	};

	class RepetitionTable
	{
	public:
		RepetitionTable() = default;
		~RepetitionTable() = default;

		void AddEntry(const std::array<Bitboard, 12>& pieceBitboards);
		void RemoveEntry(const std::array<Bitboard, 12>& pieceBitboards);

		uint8_t GetRepetitionCount(const std::array<Bitboard, 12>& pieceBitboards) const;

		void Clear();

		bool operator==(const RepetitionTable& other) const;

	private:

		std::array<RepetitionEntry, 50> m_Entries;

	};

} // namespace ChessCore