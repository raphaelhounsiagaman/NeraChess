#include "NnueEvaluator.h"

#include "SimdOps.h"

#include <array>
#include <cassert>
#include <string>

namespace NeraChessNNUE::Evaluator
{
    using namespace NeraChessEngine;

    namespace
    {
        Network& MutableNetwork()
        {
            static Network network;
            return network;
        }

        std::filesystem::path& MutablePath()
        {
            static std::filesystem::path path;
            return path;
        }

        NetworkFormat::Status s_LastStatus = NetworkFormat::Status::FileNotFound;
    }

    NetworkFormat::Status Load(const std::filesystem::path& path)
    {
        // Validate into a scratch network and swap only on success.
        // Network::LoadFromFile unloads on every failure path, so loading
        // straight into the live one turned a mistyped EvalFile into an
        // engine that evaluated every position as zero -- with the working
        // network already gone and no way back short of a restart.
        Network candidate;
        const NetworkFormat::Status status = candidate.LoadFromFile(path);
        s_LastStatus = status;
        if (status != NetworkFormat::Status::Ok)
            return status;

        MutableNetwork() = std::move(candidate);
        MutablePath() = path;
        return status;
    }

    NetworkFormat::Status LoadDefault(const std::filesystem::path& executableDirectory)
    {
        // Beside the executable first, then the bundled resource layout the
        // desktop build copies, then the working directory.
        const std::array<std::filesystem::path, 3> candidates = {
            executableDirectory.empty()
                ? std::filesystem::path{}
                : executableDirectory / DefaultNetworkName,
            executableDirectory.empty()
                ? std::filesystem::path{}
                : executableDirectory / "Resources" / "NNUE" / DefaultNetworkName,
            std::filesystem::path{ DefaultNetworkName },
        };

        for (const std::filesystem::path& candidate : candidates)
        {
            if (candidate.empty())
                continue;

            const NetworkFormat::Status status = Load(candidate);
            if (status == NetworkFormat::Status::Ok)
                return status;

            // A candidate that is simply absent is not an error; the next
            // location may hold the network. One that exists but cannot be
            // read, or was built for another architecture, is a real problem
            // and reporting FileNotFound for it hides the actual diagnosis.
            if (status != NetworkFormat::Status::FileNotFound)
                return status;
        }

        s_LastStatus = NetworkFormat::Status::FileNotFound;
        return s_LastStatus;
    }

    void Unload()
    {
        MutableNetwork().Unload();
        MutablePath().clear();
        s_LastStatus = NetworkFormat::Status::FileNotFound;
    }

    bool IsLoaded()
    {
        return MutableNetwork().IsLoaded();
    }

    const Network& GetNetwork()
    {
        return MutableNetwork();
    }

    const std::filesystem::path& LoadedPath()
    {
        return MutablePath();
    }

    std::string StatusText()
    {
        const Network& network = MutableNetwork();
        if (!network.IsLoaded())
        {
            return std::string("no network loaded (") +
                std::string(NetworkFormat::Describe(s_LastStatus)) +
                "); evaluation returns " + std::to_string(NoNetworkScore);
        }

        return "network " + MutablePath().filename().string() + ' ' +
            network.GetHeader().Describe() + ", kernels " + Simd::TargetName();
    }

    Score Evaluate(const BoardState& state, Accumulator& accumulator)
    {
        const Network& network = MutableNetwork();
        if (!network.IsLoaded())
            return NoNetworkScore;

        if (!accumulator.computed)
        {
            accumulator.Refresh(network, state);
        }
#if NNUE_VERIFY_ACCUMULATOR
        else
        {
            Accumulator reference;
            reference.Refresh(network, state);
            assert(reference.values == accumulator.values &&
                "incrementally updated accumulator diverged from a full refresh; "
                "the dirty-piece list for some move is wrong");
            assert(reference.views == accumulator.views &&
                "an accumulator half is tagged with a view the position does not "
                "imply; a king crossing the d/e boundary went unnoticed");
        }
#endif

        const Perspective sideToMove = state.HasFlag(BoardStateFlags::WhiteToMove)
            ? Perspective::White
            : Perspective::Black;
        return network.Forward(accumulator, sideToMove, Network::OutputBucketOf(state));
    }

    Score Evaluate(const ChessBoard& board)
    {
        if (!MutableNetwork().IsLoaded())
            return NoNetworkScore;

        Accumulator scratch;
        return Evaluate(board.GetBoardState(), scratch);
    }
}
