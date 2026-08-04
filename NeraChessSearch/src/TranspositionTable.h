#pragma once

#include "Move.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NeraChessSearch
{
    enum class TTBound : uint8_t
    {
        None = 0,
        Exact = 1,
        Lower = 2,
        Upper = 3,
    };

    struct alignas(16) TTEntry
    {
        uint64_t key = 0;
        uint32_t move = 0;
        int16_t score = 0;
        int8_t depth = -1;
        uint8_t generationAndBound = 0;

        TTBound GetBound() const { return static_cast<TTBound>(generationAndBound & 0x3); }
        uint8_t GetGeneration() const { return generationAndBound >> 2; }
        bool IsValid() const { return GetBound() != TTBound::None; }
    };

    struct alignas(64) TTCluster
    {
        TTEntry entries[4];
    };

    static_assert(sizeof(TTEntry) == 16);
    static_assert(sizeof(TTCluster) == 64);

    class TranspositionTable
    {
    public:
        explicit TranspositionTable(size_t megabytes = 64);

        void Resize(size_t megabytes);
        void Clear();
        void NewSearch();

        const TTEntry* Probe(uint64_t key) const;
        void Store(uint64_t key, int score, int depth, TTBound bound,
            NeraChessEngine::Move bestMove);

        int HashFullPermill() const;
        size_t SizeBytes() const { return m_Clusters.size() * sizeof(TTCluster); }

    private:
        static size_t FloorPowerOfTwo(size_t value);
        static int EntryQuality(const TTEntry& entry, uint8_t currentGeneration);
        uint8_t MakeMetadata(TTBound bound) const;

    private:
        std::vector<TTCluster> m_Clusters;
        size_t m_Mask = 0;
        uint8_t m_Generation = 1;
    };
}
