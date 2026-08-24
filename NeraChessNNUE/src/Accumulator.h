#pragma once

#include "DirtyPieces.h"
#include "FeatureSet.h"
#include "NetworkArchitecture.h"
#include "NnueCommon.h"

#include "BoardState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace NeraChessNNUE
{
    class Network;

    // The hidden layer's pre-activation values for both perspectives.
    //
    // This is the "efficiently updatable" part of NNUE: the hidden layer is a
    // plain sum of one weight column per active feature, so a move that
    // changes three features can adjust the accumulator with three column
    // operations instead of recomputing 768 x 512 products.
    struct Accumulator
    {
        // 64-byte aligned so a future SIMD path can use aligned loads.
        alignas(64) std::array<std::array<Weight, Architecture::HiddenSize>, PerspectiveCount>
            values{};

        // Input bucket each half was computed with. A move that changes a
        // perspective's bucket invalidates that half and forces a refresh of
        // it; with one bucket this never happens, but tracking it here is what
        // makes king-bucketed feature sets a local change later.
        std::array<uint8_t, PerspectiveCount> inputBuckets{};

        // False when the contents are stale and a refresh is required before
        // the accumulator may be read.
        bool computed = false;

        Weight* operator[](Perspective perspective) { return values[Index(perspective)].data(); }
        const Weight* operator[](Perspective perspective) const
        {
            return values[Index(perspective)].data();
        }

        // Recomputes both perspectives from the position. Always correct and
        // independent of move history, at the cost of touching one weight
        // column per piece on the board.
        void Refresh(const Network& network, const NeraChessEngine::BoardState& state);

        // Recomputes a single perspective, which is what a king move into a
        // different input bucket needs once bucketed feature sets exist.
        void RefreshPerspective(const Network& network,
            const NeraChessEngine::BoardState& state, Perspective perspective);

        // Applies one perspective's feature changes to an already-computed
        // accumulator.
        void ApplyDelta(const Network& network, const FeatureSet::FeatureDelta& delta,
            Perspective perspective);

        // Same as ApplyDelta, but reads the starting values from `source`
        // instead of this accumulator's own (already up to date) values, and
        // writes the result into this accumulator. Lets a child node's Push
        // fold the parent-to-child copy into its first delta operation
        // instead of copying the parent over and then rewriting it in place.
        // `source` must not alias this perspective's storage.
        void ApplyDeltaFrom(const Network& network, const Weight* source,
            const FeatureSet::FeatureDelta& delta, Perspective perspective);
    };

    // Upper bound on search depth plus quiescence extensions. Must stay at or
    // above NeraChessSearch::MAX_PLY; the stack falls back to refreshing rather
    // than overflowing if a search ever exceeds it.
    inline constexpr size_t MaxAccumulatorPly = 160;

    // Per-thread stack of accumulators, one entry per ply, mirroring the
    // search's make/unmake pairs.
    //
    // This is where NNUE earns its name. Pushing a move copies the parent's
    // accumulator and applies only the few feature columns the move changed,
    // instead of summing one column per piece on the board. Popping is free:
    // the parent's entry was never touched.
    class AccumulatorStack
    {
    public:
        AccumulatorStack();

        // Refreshes the bottom entry from the root position and discards
        // everything above it.
        void Reset(const Network& network, const NeraChessEngine::BoardState& state);

        Accumulator& Current() { return (*m_Entries)[Slot()]; }
        const Accumulator& Current() const { return (*m_Entries)[Slot()]; }

        // Enters a child node, updating its accumulator from the parent's.
        //
        // `state` is the position *after* the move and `dirty` describes what
        // the move changed. Falls back to leaving the entry stale -- correct,
        // only slower -- when the parent is stale, no network is loaded, or
        // the stack has run out of room.
        void Push(const Network& network, const NeraChessEngine::BoardState& state,
            const DirtyPieces& dirty);

        // Enters a child node without updating it, leaving the entry stale for
        // the evaluator to refresh on demand. For callers with no dirty-piece
        // list to offer.
        void PushStale();

        // Returns to the parent node.
        void Pop();

        size_t Depth() const { return m_Top; }
        bool AtCapacity() const { return m_Top + 1 >= MaxAccumulatorPly; }

    private:
        size_t Slot() const { return m_Top < MaxAccumulatorPly ? m_Top : MaxAccumulatorPly - 1; }

    private:
        using Entries = std::array<Accumulator, MaxAccumulatorPly>;

        // Heap allocated, and not merely as a style preference: the entries
        // come to roughly 330 KB, and a worker thread gets a 512 KB stack by
        // default on macOS. Holding them inline makes any SearchEngine
        // constructed on a thread's stack overflow it, which is a crash a
        // caller has no way to anticipate from the class's interface.
        std::unique_ptr<Entries> m_Entries;
        size_t m_Top = 0;
    };
}
