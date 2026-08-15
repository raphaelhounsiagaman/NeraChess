#include "Accumulator.h"

#include "Network.h"
#include "SimdOps.h"

#include <algorithm>

namespace NeraChessNNUE
{
    using namespace NeraChessEngine;

    namespace
    {
        // Input bucket a perspective's half belongs to in the given position.
        size_t BucketFor(const BoardState& state, Perspective perspective)
        {
            const uint8_t kingSquare = FeatureSet::KingSquare(state, perspective);
            return kingSquare == NoSquare ? 0u : FeatureSet::KingBucket(perspective, kingSquare);
        }
    }

    void Accumulator::RefreshPerspective(const Network& network, const BoardState& state,
        Perspective perspective)
    {
        // Without weights there is nothing to sum, and the network's storage is
        // empty, so every caller stays safe with no network loaded.
        if (!network.IsLoaded())
            return;

        Weight* target = (*this)[perspective];
        Simd::Copy(target, network.FeatureBias(), Architecture::HiddenSize);

        FeatureSet::ActiveFeatures active;
        FeatureSet::CollectActiveFeatures(state, perspective, active);
        for (size_t index = 0; index < active.count; ++index)
        {
            Simd::Add(target, network.FeatureColumn(active.indices[index]),
                Architecture::HiddenSize);
        }

        inputBuckets[Index(perspective)] = static_cast<uint8_t>(BucketFor(state, perspective));
    }

    void Accumulator::Refresh(const Network& network, const BoardState& state)
    {
        RefreshPerspective(network, state, Perspective::White);
        RefreshPerspective(network, state, Perspective::Black);
        computed = network.IsLoaded();
    }

    void Accumulator::ApplyDelta(const Network& network, const FeatureSet::FeatureDelta& delta,
        Perspective perspective)
    {
        if (!network.IsLoaded())
            return;

        Weight* target = (*this)[perspective];

        // Pair the changes up so the common quiet move -- one feature off, one
        // on -- makes a single pass over the accumulator.
        const size_t paired = std::min(delta.addedCount, delta.removedCount);
        for (size_t index = 0; index < paired; ++index)
        {
            Simd::AddSubtract(target, network.FeatureColumn(delta.added[index]),
                network.FeatureColumn(delta.removed[index]), Architecture::HiddenSize);
        }
        for (size_t index = paired; index < delta.addedCount; ++index)
            Simd::Add(target, network.FeatureColumn(delta.added[index]), Architecture::HiddenSize);
        for (size_t index = paired; index < delta.removedCount; ++index)
        {
            Simd::Subtract(target, network.FeatureColumn(delta.removed[index]),
                Architecture::HiddenSize);
        }
    }

    AccumulatorStack::AccumulatorStack()
        : m_Entries(std::make_unique<Entries>())
    {
    }

    void AccumulatorStack::Reset(const Network& network, const BoardState& state)
    {
        m_Top = 0;
        (*m_Entries)[m_Top].Refresh(network, state);
    }

    void AccumulatorStack::PushStale()
    {
        ++m_Top;
        // Past capacity the top entry is reused and marked stale on every push,
        // which stays correct at the cost of a refresh per node. Current()
        // clamps, so the depth counter may exceed the array without ever
        // indexing out of it.
        Current().computed = false;
    }

    void AccumulatorStack::Push(const Network& network, const BoardState& state,
        const DirtyPieces& dirty)
    {
        const size_t parentSlot = Slot();
        ++m_Top;

        // Out of room: reuse the top entry stale rather than aliasing the
        // parent, which an in-place delta would corrupt. MaxAccumulatorPly is
        // above the search's MAX_PLY, so this is a guard, not a normal path.
        if (m_Top >= MaxAccumulatorPly || !network.IsLoaded())
        {
            (*m_Entries)[Slot()].computed = false;
            return;
        }

        const Accumulator& parent = (*m_Entries)[parentSlot];
        Accumulator& child = (*m_Entries)[m_Top];
        if (!parent.computed)
        {
            child.computed = false;
            return;
        }

        for (const Perspective perspective : { Perspective::White, Perspective::Black })
        {
            const size_t bucket = BucketFor(state, perspective);
            if (bucket != parent.inputBuckets[Index(perspective)])
            {
                // The move moved this side's king across a bucket boundary, so
                // its half indexes a different weight matrix and no delta from
                // the parent applies. Unreachable while there is one bucket.
                child.RefreshPerspective(network, state, perspective);
                continue;
            }

            Simd::Copy(child[perspective], parent[perspective], Architecture::HiddenSize);
            child.inputBuckets[Index(perspective)] = static_cast<uint8_t>(bucket);

            FeatureSet::FeatureDelta delta;
            FeatureSet::ComputeDelta(dirty, perspective, bucket, delta);
            child.ApplyDelta(network, delta, perspective);
        }

        child.computed = true;
    }

    void AccumulatorStack::Pop()
    {
        if (m_Top > 0)
            --m_Top;
    }
}
