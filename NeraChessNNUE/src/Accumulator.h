#pragma once

#include "FeatureSet.h"
#include "NetworkArchitecture.h"
#include "NnueCommon.h"

#include "BoardState.h"

#include <array>
#include <cstddef>

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
    };

    // Upper bound on search depth plus quiescence extensions. Must stay at or
    // above NeraChessSearch::MAX_PLY; the stack falls back to refreshing rather
    // than overflowing if a search ever exceeds it.
    inline constexpr size_t MaxAccumulatorPly = 160;

    // Per-thread stack of accumulators, one entry per ply, mirroring the
    // search's make/unmake pairs.
    //
    // TODO(nnue): Push currently marks the new entry stale so the evaluator
    // falls back to a full refresh. Making Push take a DirtyPieces list and
    // call ApplyDelta is the single change that turns this from correct into
    // fast, and it is the main reason the accumulator lives in a stack at all.
    class AccumulatorStack
    {
    public:
        // Refreshes the bottom entry from the root position and discards
        // everything above it.
        void Reset(const Network& network, const NeraChessEngine::BoardState& state);

        Accumulator& Current() { return m_Entries[Slot()]; }
        const Accumulator& Current() const { return m_Entries[Slot()]; }

        // Enters a child node. The new entry starts stale.
        void Push();

        // Returns to the parent node.
        void Pop();

        size_t Depth() const { return m_Top; }
        bool AtCapacity() const { return m_Top + 1 >= MaxAccumulatorPly; }

    private:
        size_t Slot() const { return m_Top < MaxAccumulatorPly ? m_Top : MaxAccumulatorPly - 1; }

    private:
        std::array<Accumulator, MaxAccumulatorPly> m_Entries{};
        size_t m_Top = 0;
    };
}
