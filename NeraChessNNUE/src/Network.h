#pragma once

#include "Accumulator.h"
#include "NetworkArchitecture.h"
#include "NetworkFormat.h"
#include "NnueCommon.h"

#include "BoardState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace NeraChessNNUE
{
    // Quantized weights for one network, plus the forward pass over them.
    //
    // A Network is immutable once loaded and is shared by every search thread,
    // so it holds no evaluation state; all per-position state lives in an
    // Accumulator.
    class Network
    {
    public:
        Network() = default;

        bool IsLoaded() const { return m_Loaded; }
        const NetworkFormat::Header& GetHeader() const { return m_Header; }

        // Replaces the current weights. On failure the network is left
        // unloaded rather than partially populated.
        NetworkFormat::Status LoadFromFile(const std::filesystem::path& path);
        NetworkFormat::Status LoadFromMemory(std::span<const std::byte> data);

        // Writes the current weights in the format described by
        // NetworkFormat.h. Used by tests and by tooling; the trainer writes
        // its own files from Python.
        NetworkFormat::Status SaveToFile(const std::filesystem::path& path) const;

        void Unload();

        // Column of HiddenSize weights added to the accumulator when the given
        // feature is active. Feature indices already include their input
        // bucket offset.
        const Weight* FeatureColumn(FeatureIndex feature) const
        {
            return m_FeatureWeights.data() + static_cast<size_t>(feature) * Architecture::HiddenSize;
        }

        // Starting value of a freshly refreshed accumulator, before any
        // feature columns are added.
        const Weight* FeatureBias() const { return m_FeatureBias.data(); }

        // Runs the output layer over a computed accumulator and returns a
        // score from the point of view of sideToMove.
        //
        // The accumulator holds both perspectives; the output layer always
        // reads the side to move first, which is what makes the network see a
        // position and its mirror as the same problem.
        Score Forward(const Accumulator& accumulator, Perspective sideToMove,
            size_t outputBucket = 0) const;

        // Total pieces on the board -> output head.
        //
        //   pieces:  2-4  5-8  9-12 13-16 17-20 21-24 25-28 29-32
        //   bucket:    0    1     2     3     4     5     6     7
        //
        // Four piece counts to a head, which divides 2..32 evenly and is the
        // division every engine that does this arrived at. Material is a proxy
        // for phase and nothing else: it says how much is left on the board,
        // which is what makes king activity and a passed pawn mean one thing
        // in the opening and another with four pieces left.
        //
        // The table is indexed by the piece count itself rather than computing
        // (count - 1) / 4, so counts 0 and 1 land in bucket 0 instead of
        // underflowing an unsigned subtraction. Those are not legal positions,
        // but FeatureSet::ViewOf deliberately tolerates a board with no king
        // and the test suite builds them.
        static constexpr std::array<uint8_t, 33> OutputBucketTable = { {
            0, 0, 0, 0, 0, // 0-4 pieces
            1, 1, 1, 1,    // 5-8
            2, 2, 2, 2,    // 9-12
            3, 3, 3, 3,    // 13-16
            4, 4, 4, 4,    // 17-20
            5, 5, 5, 5,    // 21-24
            6, 6, 6, 6,    // 25-28
            7, 7, 7, 7,    // 29-32
        } };

        // Output head for a piece count. This is the whole definition of the
        // map; OutputBucketOf is this applied to a board. The trainer mirrors
        // it in nnue_training.architecture.output_bucket_of, and the two are
        // checked against each other through the shared feature-vector fixture.
        static constexpr size_t OutputBucketOfPieceCount(size_t pieceCount)
        {
            return OutputBucketTable[pieceCount < OutputBucketTable.size()
                    ? pieceCount
                    : OutputBucketTable.size() - 1];
        }

        // Output head for a position, from the total number of pieces on it.
        static size_t OutputBucketOf(const NeraChessEngine::BoardState& state);

    private:
        void AllocateForCurrentArchitecture();

        bool m_Loaded = false;
        NetworkFormat::Header m_Header{};

        // [input bucket][feature][neuron], flattened.
        std::vector<Weight> m_FeatureWeights;
        // [neuron]
        std::vector<Weight> m_FeatureBias;
        // [output bucket][perspective * HiddenSize + neuron], own side first.
        std::vector<Weight> m_OutputWeights;
        // [output bucket], pre-scaled by QuantizationA * QuantizationB.
        std::vector<Weight> m_OutputBias;
    };

    // The table and Architecture::OutputBucketCount are two statements of the
    // same fact -- the constant sizes the network, the table decides what the
    // heads mean -- so the same check the input buckets get applies here: a
    // layout that left a head unreachable would ship weights nothing can read,
    // and a value out of range would index past the output-weight block.
    namespace Detail
    {
        constexpr bool EveryOutputBucketIsUsed()
        {
            std::array<bool, Architecture::OutputBucketCount> seen{};
            for (const uint8_t bucket : Network::OutputBucketTable)
            {
                if (bucket >= Architecture::OutputBucketCount)
                    return false;
                seen[bucket] = true;
            }
            for (const bool used : seen)
            {
                if (!used)
                    return false;
            }
            return true;
        }
    }

    static_assert(Detail::EveryOutputBucketIsUsed(),
        "OutputBucketTable must map into [0, OutputBucketCount) and use every bucket");
}
