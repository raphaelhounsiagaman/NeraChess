#include "SelfPlay.h"

#include <charconv>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    template<typename T>
    std::optional<T> ParseNumber(std::string_view text)
    {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    void PrintUsage()
    {
        std::cout <<
R"(NeraChessSelfPlay -- generates NNUE training data without any external engine.

  Two stages break the bootstrapping problem. Material mode needs no network
  and labels random play with material balance, which trains a generation-0
  network. Self-play mode then plays real games with a network and labels them
  with search scores and game results.

Usage:
  NeraChessSelfPlay --mode material --output gen0.txt --positions 1000000
  NeraChessSelfPlay --network gen0.nnue --output gen1.txt --positions 5000000

Options:
  --mode <material|selfplay>  Generation stage (default selfplay)
  --output <path>             Sample file to write (default selfplay.txt)
  --network <path>            Network to play with (required for selfplay)
  --positions <n>             Positions to write before stopping
  --depth <n>                 Search depth per move (default 8)
  --nodes <n>                 Node limit per move instead of a depth
  --threads <n>               Parallel game workers (default hardware threads)
  --hash <mb>                 Transposition table per worker (default 16)
  --seed <n>                  Random seed; a run reproduces from it
  --random-plies <n>          Random opening plies per game (default 8)
  --max-plies <n>             Hard game length cap (default 300)
  --max-score <cp>            Drop positions scored beyond this (default 2000)
  --win-score <cp>            Adjudicate a win above this (default 800)
  --win-plies <n>             ...after this many consecutive plies (default 4)
  --draw-score <cp>           Adjudicate a draw below this (default 20)
  --draw-plies <n>            ...after this many consecutive plies (default 8)
  --draw-after <ply>          ...and not before this ply (default 120)
  --dedup-bits <n>            Duplicate filter size, 0 disables (default 22)
  --keep-checks               Keep positions where the side to move is in check
  --keep-tactical             Keep positions whose best move is a capture
  --append                    Append to the output instead of truncating it
  --quiet                     Only print the final summary
  --help                      Show this message
)";
    }
}

int main(int argc, char** argv)
{
    try
    {
        NeraChessSelfPlay::Config config;
        config.threads = std::max(1U, std::thread::hardware_concurrency());

        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        bool quiet = false;

        for (size_t index = 0; index < arguments.size(); ++index)
        {
            const std::string_view option = arguments[index];
            const auto value = [&]() -> std::string_view
            {
                if (index + 1 >= arguments.size())
                    throw std::runtime_error("missing value for " + std::string(option));
                return arguments[++index];
            };

            if (option == "--help" || option == "-h")
            {
                PrintUsage();
                return 0;
            }
            else if (option == "--mode")
            {
                const std::string_view mode = value();
                if (mode == "material")
                    config.mode = NeraChessSelfPlay::Mode::Material;
                else if (mode == "selfplay")
                    config.mode = NeraChessSelfPlay::Mode::SelfPlay;
                else
                    throw std::runtime_error("unknown mode " + std::string(mode));
            }
            else if (option == "--output") config.outputPath = value();
            else if (option == "--network") config.networkPath = value();
            else if (option == "--positions") config.targetPositions = ParseNumber<uint64_t>(value()).value_or(config.targetPositions);
            else if (option == "--depth") config.depth = ParseNumber<int>(value()).value_or(config.depth);
            else if (option == "--nodes") config.nodes = ParseNumber<uint64_t>(value()).value_or(config.nodes);
            else if (option == "--threads") config.threads = ParseNumber<size_t>(value()).value_or(config.threads);
            else if (option == "--hash") config.hashMegabytes = ParseNumber<size_t>(value()).value_or(config.hashMegabytes);
            else if (option == "--seed") config.seed = ParseNumber<uint64_t>(value()).value_or(config.seed);
            else if (option == "--random-plies") config.randomOpeningPlies = ParseNumber<int>(value()).value_or(config.randomOpeningPlies);
            else if (option == "--max-plies") config.maxGamePlies = ParseNumber<int>(value()).value_or(config.maxGamePlies);
            else if (option == "--max-score") config.maxAbsoluteScore = ParseNumber<int>(value()).value_or(config.maxAbsoluteScore);
            else if (option == "--win-score") config.winAdjudicationScore = ParseNumber<int>(value()).value_or(config.winAdjudicationScore);
            else if (option == "--win-plies") config.winAdjudicationPlies = ParseNumber<int>(value()).value_or(config.winAdjudicationPlies);
            else if (option == "--draw-score") config.drawAdjudicationScore = ParseNumber<int>(value()).value_or(config.drawAdjudicationScore);
            else if (option == "--draw-plies") config.drawAdjudicationPlies = ParseNumber<int>(value()).value_or(config.drawAdjudicationPlies);
            else if (option == "--draw-after") config.drawAdjudicationMinPly = ParseNumber<int>(value()).value_or(config.drawAdjudicationMinPly);
            else if (option == "--dedup-bits") config.deduplicationBits = ParseNumber<int>(value()).value_or(config.deduplicationBits);
            else if (option == "--keep-checks") config.skipInCheck = false;
            else if (option == "--keep-tactical") config.skipCaptureBestMove = false;
            else if (option == "--append") config.append = true;
            else if (option == "--quiet") quiet = true;
            else
                throw std::runtime_error("unknown option " + std::string(option));
        }

        if (quiet)
            config.progressInterval = 0;

        // Material mode plays randomly, so a search would only slow it down.
        if (config.mode == NeraChessSelfPlay::Mode::Material)
            config.randomOpeningPlies = 0;

        std::ostream& log = std::cout;
        const auto started = std::chrono::steady_clock::now();

        NeraChessSelfPlay::Statistics statistics;
        const int status = NeraChessSelfPlay::Run(config, statistics, log);

        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        log << statistics.Describe() << '\n'
            << "wrote " << config.outputPath << " in " << seconds << "s ("
            << static_cast<uint64_t>(statistics.positionsWritten / std::max(1e-9, seconds))
            << " positions/s)\n";
        return status;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "NeraChessSelfPlay: " << exception.what() << '\n';
        std::cerr << "Run with --help for usage.\n";
        return 2;
    }
}
