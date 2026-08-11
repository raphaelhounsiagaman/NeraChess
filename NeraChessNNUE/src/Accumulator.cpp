#include "Accumulator.h"

#include "Network.h"
#include "SimdOps.h"

#include <algorithm>

namespace NeraChessNNUE
{
    using namespace NeraChessEngine;

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

    void AccumulatorStack::Reset(const Network& network, const BoardState& state)
    {
        m_Top = 0;
        m_Entries[m_Top].Refresh(network, state);
    }

    void AccumulatorStack::Push()
    {
        ++m_Top;
        // TODO(nnue): copy the parent and apply the move's feature delta here.
        // Leaving the entry stale keeps results correct because the evaluator
        // refreshes on demand, but it throws away the whole point of an
        // incrementally updatable network.
        //
        // Past capacity the top entry is reused and marked stale on every push,
        // which stays correct at the cost of a refresh per node. Current()
        // clamps, so the depth counter may exceed the array without ever
        // indexing out of it.
        Current().computed = false;
    }

    void AccumulatorStack::Pop()
    {
        if (m_Top > 0)
            --m_Top;
    }
}
