#pragma once

#include "Accumulator.h"
#include "NetworkArchitecture.h"
#include "NetworkFormat.h"
#include "NnueCommon.h"

#include "BoardState.h"

#include <cstddef>
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

        // Output head for a position. Constant while OutputBucketCount is 1;
        // the usual refinement is to bucket by total piece count so endgames
        // and middlegames get separate heads.
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
}
