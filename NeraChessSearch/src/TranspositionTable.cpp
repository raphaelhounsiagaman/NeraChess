#include "TranspositionTable.h"

#include <algorithm>
#include <limits>

namespace NeraChessSearch
{
    namespace
    {
        constexpr uint64_t WriteLockedValidation = std::numeric_limits<uint64_t>::max();
    }

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
        const size_t requestedClusters = std::max<size_t>(1, requestedBytes / sizeof(Cluster));
        m_ClusterCount = FloorPowerOfTwo(requestedClusters);
        m_Clusters = std::make_unique<Cluster[]>(m_ClusterCount);
        m_Mask = m_ClusterCount - 1;
        m_Generation = 1;
    }

    void TranspositionTable::Clear()
    {
        for (size_t clusterIndex = 0; clusterIndex < m_ClusterCount; ++clusterIndex)
        {
            for (AtomicEntry& entry : m_Clusters[clusterIndex].entries)
            {
                entry.payload.store(0, std::memory_order_relaxed);
                entry.validation.store(0, std::memory_order_relaxed);
            }
        }
    }

    void TranspositionTable::NewSearch()
    {
        m_Generation = static_cast<uint8_t>((m_Generation + 1) & 0x3F);
        if (m_Generation == 0)
            m_Generation = 1;
    }

    uint64_t TranspositionTable::Encode(const TTEntry& entry)
    {
        return static_cast<uint64_t>(entry.move) |
            (static_cast<uint64_t>(static_cast<uint16_t>(entry.score)) << 32) |
            (static_cast<uint64_t>(static_cast<uint8_t>(entry.depth)) << 48) |
            (static_cast<uint64_t>(entry.generationAndBound) << 56);
    }

    bool TranspositionTable::IsPayloadValid(uint64_t payload)
    {
        return static_cast<TTBound>((payload >> 56) & 0x3) != TTBound::None;
    }

    TTEntry TranspositionTable::Decode(uint64_t key, uint64_t payload)
    {
        TTEntry entry;
        entry.key = key;
        entry.move = static_cast<uint32_t>(payload);
        entry.score = static_cast<int16_t>(payload >> 32);
        entry.depth = static_cast<int8_t>(payload >> 48);
        entry.generationAndBound = static_cast<uint8_t>(payload >> 56);
        return entry;
    }

    TranspositionTable::EntrySnapshot TranspositionTable::LoadSnapshot(
        const AtomicEntry& entry)
    {
        EntrySnapshot snapshot;
        const uint64_t firstValidation = entry.validation.load(std::memory_order_acquire);
        if (firstValidation == WriteLockedValidation)
            return snapshot;
        const uint64_t payload = entry.payload.load(std::memory_order_acquire);
        const uint64_t secondValidation = entry.validation.load(std::memory_order_acquire);
        if (firstValidation != secondValidation)
            return snapshot;
        snapshot.validation = firstValidation;
        snapshot.stable = true;
        if (IsPayloadValid(payload))
            snapshot.entry = Decode(firstValidation ^ payload, payload);
        return snapshot;
    }

    std::optional<TTEntry> TranspositionTable::LoadEntry(
        const AtomicEntry& entry, uint64_t key)
    {
        const EntrySnapshot snapshot = LoadSnapshot(entry);
        if (!snapshot.stable || !snapshot.entry || snapshot.entry->key != key)
            return std::nullopt;
        return snapshot.entry;
    }

    bool TranspositionTable::LockEntry(AtomicEntry& entry, uint64_t expectedValidation)
    {
        if (expectedValidation == WriteLockedValidation)
            return false;
        return entry.validation.compare_exchange_strong(expectedValidation, WriteLockedValidation,
            std::memory_order_acquire, std::memory_order_relaxed);
    }

    void TranspositionTable::PublishLocked(AtomicEntry& destination, const TTEntry& entry,
        uint64_t previousValidation)
    {
        const uint64_t payload = Encode(entry);
        const uint64_t validation = entry.key ^ payload;
        if (validation == WriteLockedValidation)
        {
            destination.validation.store(previousValidation, std::memory_order_release);
            return;
        }
        destination.payload.store(payload, std::memory_order_release);
        destination.validation.store(validation, std::memory_order_release);
    }

    std::optional<TTEntry> TranspositionTable::Probe(uint64_t key) const
    {
        const Cluster& cluster = m_Clusters[key & m_Mask];
        for (const AtomicEntry& entry : cluster.entries)
        {
            if (const std::optional<TTEntry> candidate = LoadEntry(entry, key))
                return candidate;
        }
        return std::nullopt;
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
        if (bound == TTBound::None)
            return;
        Cluster& cluster = m_Clusters[key & m_Mask];
        AtomicEntry* exact = nullptr;
        uint64_t exactValidation = 0;
        AtomicEntry* empty = nullptr;
        uint64_t emptyValidation = 0;
        AtomicEntry* victim = nullptr;
        uint64_t victimValidation = 0;
        int victimQuality = std::numeric_limits<int>::max();

        for (AtomicEntry& atomicEntry : cluster.entries)
        {
            const EntrySnapshot snapshot = LoadSnapshot(atomicEntry);
            if (!snapshot.stable)
                continue;
            if (snapshot.entry && snapshot.entry->key == key)
            {
                exact = &atomicEntry;
                exactValidation = snapshot.validation;
                break;
            }
            if (!snapshot.entry && !empty)
            {
                empty = &atomicEntry;
                emptyValidation = snapshot.validation;
            }
            if (snapshot.entry)
            {
                const int quality = EntryQuality(*snapshot.entry, m_Generation);
                if (!victim || quality < victimQuality)
                {
                    victim = &atomicEntry;
                    victimValidation = snapshot.validation;
                    victimQuality = quality;
                }
            }
        }

        AtomicEntry* replacement = exact ? exact : (empty ? empty : victim);
        const uint64_t replacementValidation = exact
            ? exactValidation
            : (empty ? emptyValidation : victimValidation);

        if (!replacement || !LockEntry(*replacement, replacementValidation))
            return;

        const uint64_t currentPayload = replacement->payload.load(std::memory_order_acquire);
        const std::optional<TTEntry> current = IsPayloadValid(currentPayload)
            ? std::optional<TTEntry>{
                Decode(replacementValidation ^ currentPayload, currentPayload) }
            : std::nullopt;

        TTEntry entry;
        const int newQuality = depth + (bound == TTBound::Exact ? 4 : 0);
        const int oldQuality = current && current->key == key
            ? static_cast<int>(current->depth) +
                (current->GetBound() == TTBound::Exact ? 4 : 0)
            : std::numeric_limits<int>::min();
        if (current && current->key == key && newQuality < oldQuality)
        {
            entry = *current;
            if (bestMove != 0)
                entry.move = bestMove;
            entry.generationAndBound = static_cast<uint8_t>(
                (m_Generation << 2) | static_cast<uint8_t>(entry.GetBound()));
        }
        else
        {
            entry.key = key;
            entry.move = bestMove;
            entry.score = static_cast<int16_t>(std::clamp(score,
                static_cast<int>(std::numeric_limits<int16_t>::min()),
                static_cast<int>(std::numeric_limits<int16_t>::max())));
            entry.depth = static_cast<int8_t>(std::clamp(depth, -1, 127));
            entry.generationAndBound = MakeMetadata(bound);
        }
        PublishLocked(*replacement, entry, replacementValidation);
    }

    int TranspositionTable::HashFullPermill() const
    {
        if (m_ClusterCount == 0)
            return 0;
        const size_t sampleClusters = std::min<size_t>(m_ClusterCount, 250);
        size_t used = 0;
        for (size_t i = 0; i < sampleClusters; ++i)
        {
            for (const AtomicEntry& atomicEntry : m_Clusters[i].entries)
            {
                const EntrySnapshot snapshot = LoadSnapshot(atomicEntry);
                used += snapshot.entry &&
                    snapshot.entry->GetGeneration() == m_Generation;
            }
        }
        return static_cast<int>(used * 1000 / (sampleClusters * 4));
    }

    size_t TranspositionTable::SizeBytes() const
    {
        return m_ClusterCount * sizeof(Cluster);
    }
}
