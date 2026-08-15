#pragma once

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>

// Training-data generation for the NNUE network.
//
// NeraChess never labels positions with another engine's evaluations, which
// leaves a bootstrapping problem: training needs search scores, search needs an
// evaluation, and the evaluation is the network being trained. This tool breaks
// that circle in two stages.
//
//   Material mode plays random legal moves and labels each position with its
//   material balance. Training on that produces a generation-0 network that
//   knows only material -- enough to make the engine play non-random chess, and
//   enough to generate real games. The knowledge lives in the data rather than
//   in a hand-written evaluation, so the engine keeps a single evaluation path
//   and generation 0 is thrown away once generation 1 exists.
//
//   Self-play mode plays games with a loaded network, labelling each position
//   with the search score and the eventual game result. Each generation trains
//   on the previous generation's games.
//
// See docs/NNUE.md for the full loop and the parameters worth tuning.

namespace NeraChessSelfPlay
{
    enum class Mode : uint8_t
    {
        // Random play, labelled with material balance. Needs no network.
        Material,
        // Games played by a loaded network, labelled with search scores.
        SelfPlay,
    };

    struct Config
    {
        Mode mode = Mode::SelfPlay;

        std::filesystem::path outputPath = "selfplay.txt";
        std::filesystem::path networkPath;

        // Stop once this many positions have been written.
        uint64_t targetPositions = 1'000'000;

        // Search limit per move. Shallow and wide beats deep and narrow:
        // position variety matters more than label precision.
        int depth = 8;
        uint64_t nodes = 0;

        size_t threads = 1;
        size_t hashMegabytes = 16;
        uint64_t seed = 0;

        // Random legal moves played before a game starts, so a deterministic
        // engine does not play the same game every time. These positions are
        // never recorded.
        int randomOpeningPlies = 8;

        int maxGamePlies = 300;

        // Positions the network has no business judging are dropped rather
        // than labelled: the score would belong to a tactic the search is
        // still resolving, not to the position.
        int maxAbsoluteScore = 2000;
        bool skipInCheck = true;
        bool skipCaptureBestMove = true;

        // Adjudication. Without it a weak network shuffles until the fifty-move
        // rule fires and nearly every label is a draw, which teaches nothing.
        int winAdjudicationScore = 800;
        int winAdjudicationPlies = 4;
        int drawAdjudicationScore = 20;
        int drawAdjudicationPlies = 8;
        int drawAdjudicationMinPly = 120;

        // Approximate duplicate rejection, sized as a power of two. Zero
        // disables it.
        int deduplicationBits = 22;

        bool append = false;
        uint64_t progressInterval = 50'000;
    };

    struct Statistics
    {
        uint64_t positionsWritten = 0;
        uint64_t gamesPlayed = 0;
        uint64_t whiteWins = 0;
        uint64_t blackWins = 0;
        uint64_t draws = 0;
        uint64_t adjudicatedWins = 0;
        uint64_t adjudicatedDraws = 0;
        uint64_t filteredPositions = 0;
        uint64_t duplicatePositions = 0;

        std::string Describe() const;
    };

    // Runs one generation pass. Returns 0 on success.
    int Run(const Config& config, Statistics& statistics, std::ostream& log);
}
