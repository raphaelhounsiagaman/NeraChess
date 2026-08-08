#pragma once

#include "ChessBoard.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace NeraChessSearch
{
    class OpeningBook
    {
    public:
        OpeningBook() = default;
        explicit OpeningBook(const std::filesystem::path& path) { Load(path); }

        bool Load(const std::filesystem::path& path);
        NeraChessEngine::Move FindMove(const NeraChessEngine::ChessBoard& board);

        bool IsAvailable() const { return m_Stream.is_open() && !m_Index.empty(); }
        size_t EntryCount() const { return m_Index.size(); }

    private:
        struct Entry
        {
            uint64_t key = 0;
            uint32_t offset = 0;
        };

        static std::string_view PositionKey(std::string_view fen);
        static uint64_t Hash(std::string_view position);

    private:
        std::ifstream m_Stream;
        std::vector<Entry> m_Index;
    };
}
