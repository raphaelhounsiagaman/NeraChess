#include "Accumulator.h"

#include "Network.h"
#include "SimdOps.h"

#include "ChessUtil.h"

#include <algorithm>
#include <cassert>

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

        views[Index(perspective)] = FeatureSet::ViewOf(state, perspective);
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

    void Accumulator::ApplyDeltaFrom(const Network& network, const Weight* source,
        const FeatureSet::FeatureDelta& delta, Perspective perspective)
    {
        if (!network.IsLoaded())
            return;

        Weight* target = (*this)[perspective];
        // Push's out-of-room guard (m_Top >= MaxAccumulatorPly) is what keeps
        // this from ever aliasing: it is the one case where two plies would
        // otherwise clamp onto the same entry.
        assert(target != source &&
            "ApplyDeltaFrom's source must not alias the accumulator it writes");

        const size_t paired = std::min(delta.addedCount, delta.removedCount);

        // The first delta operation reads `source` (the parent) and writes
        // `target` (this, the child) directly, folding in the copy that a
        // plain ApplyDelta would need done ahead of time. Every operation
        // after the first is in place on `target`, exactly as in ApplyDelta.
        if (paired > 0)
        {
            Simd::CopyAddSubtract(target, source, network.FeatureColumn(delta.added[0]),
                network.FeatureColumn(delta.removed[0]), Architecture::HiddenSize);
        }
        else if (delta.addedCount > 0)
        {
            Simd::CopyAdd(target, source, network.FeatureColumn(delta.added[0]),
                Architecture::HiddenSize);
        }
        else if (delta.removedCount > 0)
        {
            Simd::CopySubtract(target, source, network.FeatureColumn(delta.removed[0]),
                Architecture::HiddenSize);
        }
        else
        {
            Simd::Copy(target, source, Architecture::HiddenSize);
        }

        for (size_t index = 1; index < paired; ++index)
        {
            Simd::AddSubtract(target, network.FeatureColumn(delta.added[index]),
                network.FeatureColumn(delta.removed[index]), Architecture::HiddenSize);
        }
        for (size_t index = std::max<size_t>(paired, 1); index < delta.addedCount; ++index)
            Simd::Add(target, network.FeatureColumn(delta.added[index]), Architecture::HiddenSize);
        for (size_t index = std::max<size_t>(paired, 1); index < delta.removedCount; ++index)
        {
            Simd::Subtract(target, network.FeatureColumn(delta.removed[index]),
                Architecture::HiddenSize);
        }
    }

    void RefreshCache::Clear()
    {
        for (auto& perspectiveEntries : entries)
            for (auto& bucketEntries : perspectiveEntries)
                for (Entry& entry : bucketEntries)
                    entry.populated = false;
    }

    void RefreshCache::RefreshInto(const Network& network, const BoardState& state,
        const FeatureSet::View& view, Weight* target)
    {
        Entry& entry = Slot(view);

        if (!entry.populated)
        {
            // Nothing to diff against yet, so pay for one full sum and keep
            // the result. Every later visit to this view amortizes it.
            Simd::Copy(entry.values.data(), network.FeatureBias(), Architecture::HiddenSize);

            FeatureSet::ActiveFeatures active;
            FeatureSet::CollectActiveFeatures(state, view.perspective, active);
            for (size_t index = 0; index < active.count; ++index)
            {
                Simd::Add(entry.values.data(), network.FeatureColumn(active.indices[index]),
                    Architecture::HiddenSize);
            }

            entry.pieces = state.pieceBitboards;
            entry.populated = true;
            Simd::Copy(target, entry.values.data(), Architecture::HiddenSize);
            return;
        }

        // The view fixes the numbering, so a piece contributes the same
        // feature in both positions and only the squares that differ matter.
        // Walking the two bitboards for each piece type gives exactly the
        // columns to add and to remove, with no move history involved.
        for (size_t piece = 0; piece < entry.pieces.size(); ++piece)
        {
            const Bitboard cached = entry.pieces[piece];
            const Bitboard current = state.pieceBitboards[piece];
            if (cached == current)
                continue;

            const Piece kind{ static_cast<uint8_t>(piece) };

            Bitboard added = current & ~cached;
            while (added)
            {
                const uint8_t square = BitUtil::GetLSBIndex(added);
                added &= added - 1;
                Simd::Add(entry.values.data(),
                    network.FeatureColumn(FeatureSet::FeatureIndexOf(view, kind, square)),
                    Architecture::HiddenSize);
            }

            Bitboard removed = cached & ~current;
            while (removed)
            {
                const uint8_t square = BitUtil::GetLSBIndex(removed);
                removed &= removed - 1;
                Simd::Subtract(entry.values.data(),
                    network.FeatureColumn(FeatureSet::FeatureIndexOf(view, kind, square)),
                    Architecture::HiddenSize);
            }
        }

        entry.pieces = state.pieceBitboards;
        Simd::Copy(target, entry.values.data(), Architecture::HiddenSize);
    }

    AccumulatorStack::AccumulatorStack()
        : m_Entries(std::make_unique<Entries>())
        , m_RefreshCache(std::make_unique<RefreshCache>())
    {
    }

    void AccumulatorStack::RefreshPerspectiveCached(const Network& network,
        const BoardState& state, Accumulator& entry, Perspective perspective)
    {
        if (!network.IsLoaded())
            return;

        const FeatureSet::View view = FeatureSet::ViewOf(state, perspective);
        m_RefreshCache->RefreshInto(network, state, view, entry[perspective]);
        entry.views[Index(perspective)] = view;
    }

    void AccumulatorStack::Reset(const Network& network, const BoardState& state)
    {
        m_Top = 0;

        // A new root is a new game or a new search, and the cached positions
        // belong to the old one. They would still be *correct* to diff from --
        // the diff is over bitboards, not history -- but an unrelated position
        // makes for a large one, so the first refresh of each view would cost
        // more than the full sum it replaced.
        m_RefreshCache->Clear();

        Accumulator& root = (*m_Entries)[m_Top];
        RefreshPerspectiveCached(network, state, root, Perspective::White);
        RefreshPerspectiveCached(network, state, root, Perspective::Black);
        root.computed = network.IsLoaded();
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
            const FeatureSet::View view = FeatureSet::ViewOf(state, perspective);
            if (view != parent.views[Index(perspective)])
            {
                // The move took this side's own king across the d/e boundary
                // (or, once buckets exist, into another bucket), so its half
                // numbers every feature differently than the parent's does and
                // no delta from the parent applies.
                //
                // Only this half, though. The other perspective's own king
                // did not move, so its view still stands and it takes the
                // ordinary delta below: it sees the king move the way it sees
                // any other enemy piece move.
                RefreshPerspectiveCached(network, state, child, perspective);
                continue;
            }

            child.views[Index(perspective)] = view;

            FeatureSet::FeatureDelta delta;
            FeatureSet::ComputeDelta(dirty, view, delta);
            child.ApplyDeltaFrom(network, parent[perspective], delta, perspective);
        }

        child.computed = true;
    }

    void AccumulatorStack::Pop()
    {
        if (m_Top > 0)
            --m_Top;
    }
}
