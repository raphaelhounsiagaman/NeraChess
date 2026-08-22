#include "SelfPlay.h"

#include <algorithm>
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

    // A malformed number is an operator mistake, not a request for the default.
    // Silently substituting one produces a run whose settings are not the ones
    // that were asked for, which is worse than refusing to start.
    template<typename T>
    T RequireNumber(std::string_view option, std::string_view text)
    {
        if (const auto value = ParseNumber<T>(text))
            return *value;
        throw std::runtime_error(
            std::string(option) + " expects a number, got '" + std::string(text) + "'");
    }

    template<typename T>
    void RequireAtLeast(std::string_view option, T value, T minimum)
    {
        if (value < minimum)
            throw std::runtime_error(std::string(option) + " must be at least " +
                std::to_string(minimum) + ", got " + std::to_string(value));
    }

    template<typename T>
    void RequireInRange(std::string_view option, T value, T minimum, T maximum)
    {
        if (value < minimum || value > maximum)
            throw std::runtime_error(std::string(option) + " must be between " +
                std::to_string(minimum) + " and " + std::to_string(maximum) +
                ", got " + std::to_string(value));
    }

    // Checked before the output file is opened, so a rejected run leaves no
    // truncated file behind.
    void ValidateConfig(const NeraChessSelfPlay::Config& config)
    {
        RequireAtLeast<uint64_t>("--positions", config.targetPositions, 1);

        // A search needs one of the two budgets. --nodes overrides --depth, so
        // a zero depth is only fatal when no node budget replaces it.
        if (config.mode == NeraChessSelfPlay::Mode::SelfPlay && config.nodes == 0)
            RequireAtLeast("--depth", config.depth, 1);

        RequireAtLeast<size_t>("--threads", config.threads, 1);
        RequireAtLeast<size_t>("--hash", config.hashMegabytes, 1);
        RequireAtLeast("--random-plies", config.randomOpeningPlies, 0);

        // Zero plies means every game yields no samples, so the worker loop
        // would never reach the target and never stop.
        RequireAtLeast("--max-plies", config.maxGamePlies, 1);

        RequireAtLeast("--max-score", config.maxAbsoluteScore, 1);
        RequireAtLeast("--win-score", config.winAdjudicationScore, 1);
        RequireAtLeast("--win-plies", config.winAdjudicationPlies, 1);
        RequireAtLeast("--draw-score", config.drawAdjudicationScore, 0);
        RequireAtLeast("--draw-plies", config.drawAdjudicationPlies, 1);
        RequireAtLeast("--draw-after", config.drawAdjudicationMinPly, 0);

        // The filter allocates 2^bits slots. Anything past 32 asks for an
        // allocation no machine will satisfy, and a negative shift is
        // undefined behaviour.
        RequireInRange("--dedup-bits", config.deduplicationBits, 0, 32);

        if (config.mode == NeraChessSelfPlay::Mode::SelfPlay && config.networkPath.empty())
            throw std::runtime_error("--network is required for selfplay mode");

        if (config.outputPath.empty())
            throw std::runtime_error("--output must not be empty");
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
            else if (option == "--positions") config.targetPositions = RequireNumber<uint64_t>(option, value());
            else if (option == "--depth") config.depth = RequireNumber<int>(option, value());
            else if (option == "--nodes") config.nodes = RequireNumber<uint64_t>(option, value());
            else if (option == "--threads") config.threads = RequireNumber<size_t>(option, value());
            else if (option == "--hash") config.hashMegabytes = RequireNumber<size_t>(option, value());
            else if (option == "--seed") config.seed = RequireNumber<uint64_t>(option, value());
            else if (option == "--random-plies") config.randomOpeningPlies = RequireNumber<int>(option, value());
            else if (option == "--max-plies") config.maxGamePlies = RequireNumber<int>(option, value());
            else if (option == "--max-score") config.maxAbsoluteScore = RequireNumber<int>(option, value());
            else if (option == "--win-score") config.winAdjudicationScore = RequireNumber<int>(option, value());
            else if (option == "--win-plies") config.winAdjudicationPlies = RequireNumber<int>(option, value());
            else if (option == "--draw-score") config.drawAdjudicationScore = RequireNumber<int>(option, value());
            else if (option == "--draw-plies") config.drawAdjudicationPlies = RequireNumber<int>(option, value());
            else if (option == "--draw-after") config.drawAdjudicationMinPly = RequireNumber<int>(option, value());
            else if (option == "--dedup-bits") config.deduplicationBits = RequireNumber<int>(option, value());
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

        ValidateConfig(config);

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
