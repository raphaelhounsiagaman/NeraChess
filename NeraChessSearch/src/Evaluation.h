#pragma once

#include "Accumulator.h"
#include "ChessBoard.h"

#include <cstdint>
#include <filesystem>
#include <string>

// Search-facing evaluation facade.
//
// The hand-crafted evaluation -- tapered piece-square tables, mobility, pawn
// structure, and king safety -- was removed when this branch started, and all
// positional judgement now comes from the NNUE network in NeraChessNNUE.
// Until a trained network exists, Evaluate returns a constant, so the engine
// on this branch plays no better than its search alone. See docs/NNUE.md.
//
// This header exists so that search never includes NNUE internals directly:
// only the facade knows which evaluator is in use.

namespace NeraChessSearch::Evaluation
{
    // Static evaluation from the side-to-move point of view, in centipawns.
    // Refreshes a scratch accumulator, so prefer the accumulator overload on
    // hot search paths.
    int32_t Evaluate(const NeraChessEngine::ChessBoard& board);

    // Static evaluation reusing a caller-owned accumulator, refreshing it only
    // when it is stale.
    int32_t Evaluate(const NeraChessEngine::BoardState& state,
        NeraChessNNUE::Accumulator& accumulator);

    // Whether a network is loaded. False means Evaluate is a constant and any
    // strength measurement on this build is meaningless.
    bool IsNetworkLoaded();

    // Loads a network, replacing any current one. Must not be called while a
    // search is running.
    bool LoadNetwork(const std::filesystem::path& path, std::string& message);

    // Attempts to load the default network from the executable's directory and
    // then the working directory. Having none is not an error; call StatusText
    // for the outcome.
    void LoadDefaultNetwork(const std::filesystem::path& executableDirectory);

    // One-line description of the evaluator's state, for UCI info strings.
    std::string StatusText();
}
