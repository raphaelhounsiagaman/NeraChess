#pragma once

#include "Accumulator.h"
#include "Network.h"
#include "NetworkFormat.h"
#include "NnueCommon.h"

#include "ChessBoard.h"

#include <filesystem>
#include <string>

// The engine's entry point into NNUE evaluation.
//
// One network is shared by the whole process. It is immutable while a search
// runs, so every search thread may read it concurrently; loading or unloading
// must happen with all searches stopped, which is what UciSession::SetOption
// guarantees by calling StopSearch first.

namespace NeraChessNNUE::Evaluator
{
    // Returned by Evaluate when no network is loaded. A flat zero makes the
    // engine play by search alone, which is deliberately useless but keeps
    // every caller total and makes the missing network obvious rather than
    // subtly weak.
    inline constexpr Score NoNetworkScore = 0;

    // Default file name looked up next to the executable and in the working
    // directory when no explicit path is configured.
    inline constexpr const char* DefaultNetworkName = "nera.nnue";

    NetworkFormat::Status Load(const std::filesystem::path& path);

    // Tries DefaultNetworkName in the usual locations. Returns FileNotFound
    // without complaining when there is nothing to load, since a source build
    // legitimately has no network yet.
    NetworkFormat::Status LoadDefault(const std::filesystem::path& executableDirectory = {});

    void Unload();
    bool IsLoaded();

    const Network& GetNetwork();

    // Path of the currently loaded network, or an empty path.
    const std::filesystem::path& LoadedPath();

    // One-line description for UCI "info string" output: either the loaded
    // network's shape and kernel target, or why there is none.
    std::string StatusText();

    // Evaluates from the side-to-move point of view, in centipawns.
    //
    // Refreshes a scratch accumulator on every call. Correct for any position
    // and any caller, and slow enough that it must not survive past the point
    // where the search drives an AccumulatorStack.
    Score Evaluate(const NeraChessEngine::ChessBoard& board);

    // Evaluates using an accumulator the caller maintains, refreshing it first
    // if it is stale. This is the form the search should eventually use.
    Score Evaluate(const NeraChessEngine::BoardState& state, Accumulator& accumulator);
}
