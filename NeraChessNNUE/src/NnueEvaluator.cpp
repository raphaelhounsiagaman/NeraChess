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
        const NetworkFormat::Status status = MutableNetwork().LoadFromFile(path);
        s_LastStatus = status;
        MutablePath() = status == NetworkFormat::Status::Ok ? path : std::filesystem::path{};
        return status;
    }

    NetworkFormat::Status LoadDefault(const std::filesystem::path& executableDirectory)
    {
        const std::array<std::filesystem::path, 2> candidates = {
            executableDirectory.empty()
                ? std::filesystem::path{}
                : executableDirectory / DefaultNetworkName,
            std::filesystem::path{ DefaultNetworkName },
        };

        for (const std::filesystem::path& candidate : candidates)
        {
            if (candidate.empty())
                continue;
            if (Load(candidate) == NetworkFormat::Status::Ok)
                return NetworkFormat::Status::Ok;
        }

        Unload();
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
            network.GetHeader().Describe() + ", kernels " + Simd::TargetName;
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
