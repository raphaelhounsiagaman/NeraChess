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

        // The view each half was computed under: its input bucket and its
        // horizontal orientation. A move that changes a perspective's view --
        // its own king crossing the d/e boundary, or changing bucket once
        // buckets exist -- renumbers every feature of that half, so no delta
        // from the parent applies and that half has to be refreshed.
        std::array<FeatureSet::View, PerspectiveCount> views{
            FeatureSet::View{ Perspective::White, FeatureSet::Orientation::Direct, 0 },
            FeatureSet::View{ Perspective::Black, FeatureSet::Orientation::Direct, 0 },
        };

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

        // Recomputes a single perspective, which is what a king move that
        // changes that side's view needs: crossing the d/e boundary flips its
        // horizontal orientation, and every one of its features with it.
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

    // Last accumulator half computed for each view, kept so that a refresh can
    // start from a related position instead of from the bias vector.
    //
    // A refresh sums one weight column per piece on the board -- up to 32 --
    // where an incremental update touches two or three. King buckets make that
    // matter: every king move that changes bucket forces one, where before
    // only a king crossing the d/e boundary did. Measured on this machine, the
    // eight-bucket feature set cost about 5% of nodes per second without this
    // cache.
    //
    // The trick is that a view fixes the whole numbering -- perspective,
    // bucket and orientation -- so within one slot a feature index depends
    // only on (piece, square). Two positions that share a view therefore
    // differ by exactly the pieces whose bitboards differ, however many moves
    // apart they are. A search returning to a bucket it has visited before
    // usually finds the board nearly unchanged, so the diff is small.
    //
    // This is a cache in the strict sense: dropping any entry changes only
    // speed. Entries are per-thread, because they are reached only through an
    // AccumulatorStack, which is itself per-thread.
    struct RefreshCache
    {
        struct Entry
        {
            alignas(64) std::array<Weight, Architecture::HiddenSize> values{};

            // The position those values were summed from. Compared piece
            // bitboard by piece bitboard to find what to add and remove.
            std::array<NeraChessEngine::Bitboard, 12> pieces{};

            // False until the entry has been filled once, after which the
            // stored values are the accumulator for `pieces` under this slot's
            // view. There is no way to invalidate an entry and no need for
            // one: it is only ever read as a starting point to diff from.
            bool populated = false;
        };

        // One entry per (perspective, bucket, orientation). Both orientations
        // are kept because a bucket is reached in both, and a slot holding
        // the wrong orientation would have to be rebuilt on every alternation.
        std::array<std::array<std::array<Entry, 2>, Architecture::InputBucketCount>,
            PerspectiveCount>
            entries{};

        Entry& Slot(const FeatureSet::View& view)
        {
            return entries[Index(view.perspective)][view.inputBucket]
                          [view.orientation == FeatureSet::Orientation::Mirrored ? 1u : 0u];
        }

        void Clear();

        // Brings `target` up to date for `state` under `view`, using the
        // matching entry as a starting point and refreshing from the bias
        // vector when there is none. Updates the entry to match `state`.
        void RefreshInto(const Network& network, const NeraChessEngine::BoardState& state,
            const FeatureSet::View& view, Weight* target);
    };

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

        // Refreshes one half of `entry` through the cache. Same result as
        // Accumulator::RefreshPerspective, reached by a cheaper route.
        void RefreshPerspectiveCached(const Network& network,
            const NeraChessEngine::BoardState& state, Accumulator& entry,
            Perspective perspective);

    private:
        using Entries = std::array<Accumulator, MaxAccumulatorPly>;

        // Heap allocated, and not merely as a style preference: the entries
        // come to roughly 330 KB, and a worker thread gets a 512 KB stack by
        // default on macOS. Holding them inline makes any SearchEngine
        // constructed on a thread's stack overflow it, which is a crash a
        // caller has no way to anticipate from the class's interface.
        std::unique_ptr<Entries> m_Entries;

        // Heap allocated for the same reason as the entries: one per
        // perspective, bucket and orientation comes to a few hundred KB.
        std::unique_ptr<RefreshCache> m_RefreshCache;
        size_t m_Top = 0;
    };
}
