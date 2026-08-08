#include "TranspositionTable.h"

#include <algorithm>
#include <limits>

namespace NeraChessSearch
{
    TranspositionTable::TranspositionTable(size_t megabytes)
    {
        Resize(megabytes);
    }

    size_t TranspositionTable::FloorPowerOfTwo(size_t value)
    {
        size_t result = 1;
        while (result <= value / 2)
            result <<= 1;
        return result;
    }

    void TranspositionTable::Resize(size_t megabytes)
    {
        const size_t requestedBytes = std::max<size_t>(1, megabytes) * 1024ULL * 1024ULL;
        const size_t requestedClusters = std::max<size_t>(1, requestedBytes / sizeof(TTCluster));
        const size_t clusterCount = FloorPowerOfTwo(requestedClusters);
        m_Clusters.assign(clusterCount, TTCluster{});
        m_Mask = clusterCount - 1;
        m_Generation = 1;
    }

    void TranspositionTable::Clear()
    {
        std::fill(m_Clusters.begin(), m_Clusters.end(), TTCluster{});
    }

    void TranspositionTable::NewSearch()
    {
        m_Generation = static_cast<uint8_t>((m_Generation + 1) & 0x3F);
        if (m_Generation == 0)
            m_Generation = 1;
    }

    const TTEntry* TranspositionTable::Probe(uint64_t key) const
    {
        const TTCluster& cluster = m_Clusters[key & m_Mask];
        for (const TTEntry& entry : cluster.entries)
        {
            if (entry.IsValid() && entry.key == key)
                return &entry;
        }
        return nullptr;
    }

    uint8_t TranspositionTable::MakeMetadata(TTBound bound) const
    {
        return static_cast<uint8_t>((m_Generation << 2) | static_cast<uint8_t>(bound));
    }

    int TranspositionTable::EntryQuality(const TTEntry& entry, uint8_t currentGeneration)
    {
        if (!entry.IsValid())
            return std::numeric_limits<int>::min();
        const int age = (currentGeneration - entry.GetGeneration()) & 0x3F;
        const int exactBonus = entry.GetBound() == TTBound::Exact ? 4 : 0;
        return static_cast<int>(entry.depth) + exactBonus - age * 4;
    }

    void TranspositionTable::Store(uint64_t key, int score, int depth, TTBound bound,
        NeraChessEngine::Move bestMove)
    {
        TTCluster& cluster = m_Clusters[key & m_Mask];
        TTEntry* replacement = nullptr;

        for (TTEntry& entry : cluster.entries)
        {
            if (entry.IsValid() && entry.key == key)
            {
                const int oldQuality = static_cast<int>(entry.depth) +
                    (entry.GetBound() == TTBound::Exact ? 4 : 0);
                const int newQuality = depth + (bound == TTBound::Exact ? 4 : 0);
                if (newQuality >= oldQuality)
                {
                    replacement = &entry;
                }
                else
                {
                    if (bestMove != 0)
                        entry.move = bestMove;
                    entry.generationAndBound = static_cast<uint8_t>(
                        (m_Generation << 2) | static_cast<uint8_t>(entry.GetBound()));
                    return;
                }
                break;
            }
            if (!entry.IsValid())
            {
                replacement = &entry;
                break;
            }
        }

        if (!replacement)
        {
            replacement = &cluster.entries[0];
            for (TTEntry& entry : cluster.entries)
            {
                if (EntryQuality(entry, m_Generation) < EntryQuality(*replacement, m_Generation))
                    replacement = &entry;
            }
        }

        replacement->key = key;
        replacement->move = bestMove;
        replacement->score = static_cast<int16_t>(std::clamp(score,
            static_cast<int>(std::numeric_limits<int16_t>::min()),
            static_cast<int>(std::numeric_limits<int16_t>::max())));
        replacement->depth = static_cast<int8_t>(std::clamp(depth, -1, 127));
        replacement->generationAndBound = MakeMetadata(bound);
    }

    int TranspositionTable::HashFullPermill() const
    {
        if (m_Clusters.empty())
            return 0;
        const size_t sampleClusters = std::min<size_t>(m_Clusters.size(), 250);
        size_t used = 0;
        for (size_t i = 0; i < sampleClusters; ++i)
        {
            for (const TTEntry& entry : m_Clusters[i].entries)
                used += entry.IsValid() && entry.GetGeneration() == m_Generation;
        }
        return static_cast<int>(used * 1000 / (sampleClusters * 4));
    }
}
