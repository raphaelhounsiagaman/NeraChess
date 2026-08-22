#pragma once

#include "Accumulator.h"
#include "ChessBoard.h"

#include <cstdint>
#include <filesystem>
#include <string>

// Search-facing evaluation facade.
//
// All positional judgement comes from the NNUE network in NeraChessNNUE;
// there is no hand-crafted evaluation. With no network loaded Evaluate
// returns a constant, so the engine plays no better than its search alone.
// See docs/NNUE.md.
//
// This header is where the choice of evaluator lives, so changing how a
// position is scored touches one file. It is not a complete boundary:
// the accumulator overload below takes an NNUE type, and SearchEngine holds
// an AccumulatorStack and a Network pointer in its own header.

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
