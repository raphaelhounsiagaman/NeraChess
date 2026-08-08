#pragma once

#include "Move.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace NeraChessSearch
{
    enum class TTBound : uint8_t
    {
        None = 0,
        Exact = 1,
        Lower = 2,
        Upper = 3,
    };

    struct TTEntry
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

    static_assert(sizeof(TTEntry) == 16);

    class TranspositionTable
    {
    public:
        explicit TranspositionTable(size_t megabytes = 64);

        // The search coordinator calls these only while no worker is accessing the table.
        void Resize(size_t megabytes);
        void Clear();
        void NewSearch();

        std::optional<TTEntry> Probe(uint64_t key) const;
        void Store(uint64_t key, int score, int depth, TTBound bound,
            NeraChessEngine::Move bestMove);

        int HashFullPermill() const;
        size_t SizeBytes() const;

    private:
        struct alignas(16) AtomicEntry
        {
            // A committed entry satisfies validation == zobristKey ^ payload.
            std::atomic<uint64_t> validation{ 0 };
            std::atomic<uint64_t> payload{ 0 };
        };

        struct alignas(64) Cluster
        {
            AtomicEntry entries[4];
        };

        struct EntrySnapshot
        {
            uint64_t validation = 0;
            std::optional<TTEntry> entry;
            bool stable = false;
        };

        static_assert(sizeof(AtomicEntry) == 16);
        static_assert(sizeof(Cluster) == 64);
        static_assert(std::atomic<uint64_t>::is_always_lock_free,
            "The shared transposition table requires lock-free 64-bit atomics");

        static size_t FloorPowerOfTwo(size_t value);
        static int EntryQuality(const TTEntry& entry, uint8_t currentGeneration);
        static bool IsPayloadValid(uint64_t payload);
        static uint64_t Encode(const TTEntry& entry);
        static TTEntry Decode(uint64_t key, uint64_t payload);
        static EntrySnapshot LoadSnapshot(const AtomicEntry& entry);
        static std::optional<TTEntry> LoadEntry(const AtomicEntry& entry, uint64_t key);
        static bool LockEntry(AtomicEntry& entry, uint64_t expectedValidation);
        static void PublishLocked(AtomicEntry& destination, const TTEntry& entry,
            uint64_t previousValidation);
        uint8_t MakeMetadata(TTBound bound) const;

    private:
        std::unique_ptr<Cluster[]> m_Clusters;
        size_t m_ClusterCount = 0;
        size_t m_Mask = 0;
        uint8_t m_Generation = 1;
    };
}
