#include "OpeningBook.h"

#include <algorithm>
#include <limits>
#include <string>

namespace NeraChessSearch
{
    std::string_view OpeningBook::PositionKey(std::string_view fen)
    {
        size_t spaces = 0;
        for (size_t index = 0; index < fen.size(); ++index)
        {
            if (fen[index] == ' ' && ++spaces == 4)
                return fen.substr(0, index);
        }
        return fen;
    }

    uint64_t OpeningBook::Hash(std::string_view position)
    {
        uint64_t hash = 14'695'981'039'346'656'037ULL;
        for (const unsigned char character : position)
        {
            hash ^= character;
            hash *= 1'099'511'628'211ULL;
        }
        return hash;
    }

    bool OpeningBook::Load(const std::filesystem::path& path)
    {
        m_Stream.close();
        m_Stream.clear();
        m_Index.clear();
        m_Stream.open(path, std::ios::binary);
        if (!m_Stream)
            return false;

        std::string line;
        while (true)
        {
            const std::streamoff offset = m_Stream.tellg();
            if (!std::getline(m_Stream, line))
                break;
            const size_t separator = line.find(',');
            if (separator == std::string::npos || offset < 0 ||
                static_cast<uint64_t>(offset) > std::numeric_limits<uint32_t>::max())
            {
                continue;
            }
            const std::string_view fen(line.data(), separator);
            m_Index.push_back({ Hash(PositionKey(fen)),
                static_cast<uint32_t>(offset) });
        }

        std::sort(m_Index.begin(), m_Index.end(), [](const Entry& left, const Entry& right)
        {
            if (left.key != right.key)
                return left.key < right.key;
            return left.offset < right.offset;
        });
        m_Stream.clear();
        m_Stream.seekg(0);
        return !m_Index.empty();
    }

    NeraChessEngine::Move OpeningBook::FindMove(const NeraChessEngine::ChessBoard& board)
    {
        if (!IsAvailable())
            return 0;

        const std::string fen = board.GetFENString();
        const std::string_view position = PositionKey(fen);
        const uint64_t key = Hash(position);
        auto entry = std::lower_bound(m_Index.begin(), m_Index.end(), key,
            [](const Entry& candidate, uint64_t candidateKey)
            {
                return candidate.key < candidateKey;
            });

        const NeraChessEngine::MoveList<218> legalMoves = board.GetLegalMoves();
        std::vector<int> frequencies(legalMoves.size(), 0);
        std::string line;
        for (; entry != m_Index.end() && entry->key == key; ++entry)
        {
            m_Stream.clear();
            m_Stream.seekg(entry->offset);
            if (!std::getline(m_Stream, line))
                continue;
            const size_t separator = line.find(',');
            if (separator == std::string::npos ||
                PositionKey(std::string_view(line.data(), separator)) != position)
                continue;

            std::string_view uci(line.data() + separator + 1, line.size() - separator - 1);
            if (!uci.empty() && uci.back() == '\r')
                uci.remove_suffix(1);
            for (size_t index = 0; index < legalMoves.size(); ++index)
            {
                if (legalMoves[index].ToUCI() == uci)
                {
                    ++frequencies[index];
                    break;
                }
            }
        }

        const auto best = std::max_element(frequencies.begin(), frequencies.end());
        if (best == frequencies.end() || *best == 0)
            return 0;
        return legalMoves[static_cast<size_t>(best - frequencies.begin())];
    }
}
