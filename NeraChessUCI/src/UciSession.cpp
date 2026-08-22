#include "UciSession.h"

#include "Evaluation.h"
#include "NnueEvaluator.h"
#include "TimeManagement.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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

    std::vector<std::string> Tokenize(std::string_view command)
    {
        std::istringstream stream{ std::string(command) };
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token)
            tokens.push_back(std::move(token));
        return tokens;
    }

    std::string Join(const std::vector<std::string>& tokens, size_t begin, size_t end)
    {
        std::string joined;
        for (size_t index = begin; index < end; ++index)
        {
            if (!joined.empty())
                joined += ' ';
            joined += tokens[index];
        }
        return joined;
    }

    NeraChessEngine::Move FindLegalMove(const NeraChessEngine::ChessBoard& board,
        std::string_view uci)
    {
        for (const NeraChessEngine::Move move : board.GetLegalMoves())
        {
            if (move.ToUCI() == uci)
                return move;
        }
        return 0;
    }
}

UciSession::UciSession(std::istream& input, std::ostream& output,
    std::filesystem::path openingBookPath, std::filesystem::path executableDirectory)
    : m_Input(input), m_Output(output),
      m_OpeningBookPath(std::move(openingBookPath)),
      m_ExecutableDirectory(std::move(executableDirectory)),
      m_OpeningBook(m_OpeningBookPath)
{
    // Pick up a network sitting beside the executable so a packaged build
    // needs no configuration. A source build normally has none, which is not
    // an error at this stage of the NNUE work; the "uci" handshake reports the
    // outcome either way.
    NeraChessSearch::Evaluation::LoadDefaultNetwork(m_ExecutableDirectory);
    if (NeraChessSearch::Evaluation::IsNetworkLoaded())
        m_NetworkPath = NeraChessNNUE::Evaluator::LoadedPath();
}

UciSession::~UciSession()
{
    StopSearch(false);
}

int UciSession::Run()
{
    std::string line;
    while (std::getline(m_Input, line))
    {
        if (!HandleCommand(line))
            break;
    }
    StopSearch(false);
    return 0;
}

bool UciSession::HandleCommand(const std::string& line)
{
    if (line == "uci")
    {
        Print("id name NeraChess");
        Print("id author Raphael Hounsiagaman");
        Print("option name Hash type spin default 64 min 1 max 1024");
        Print("option name Threads type spin default 1 min 1 max 256");
        Print("option name Clear Hash type button");
        Print("option name OwnBook type check default true");
        Print("option name BookFile type string default " + m_OpeningBookPath.string());
        Print("option name Ponder type check default false");
        Print("option name EvalFile type string default " + m_NetworkPath.string());
        if (m_OpeningBook.IsAvailable())
        {
            Print("info string opening book loaded with " +
                std::to_string(m_OpeningBook.EntryCount()) + " entries");
        }
        else
        {
            Print("info string opening book unavailable; searches will use the engine");
        }
        Print("info string " + NeraChessSearch::Evaluation::StatusText());
        if (!NeraChessSearch::Evaluation::IsNetworkLoaded())
        {
            Print("info string this build has no trained network; "
                "set EvalFile before expecting reasonable play");
        }
        Print("uciok");
    }
    else if (line == "isready")
    {
        Print("readyok");
    }
    else if (line == "ucinewgame")
    {
        StopSearch();
        m_Board = NeraChessEngine::ChessBoard();
        m_SearchEngine.NewGame();
    }
    else if (line.starts_with("setoption "))
    {
        SetOption(line);
    }
    else if (line.starts_with("position "))
    {
        SetPosition(line);
    }
    else if (line == "go" || line.starts_with("go "))
    {
        StartSearch(line);
    }
    else if (line == "stop")
    {
        StopSearch();
    }
    else if (line == "ponderhit")
    {
        PonderHit();
    }
    else if (line == "d")
    {
        Print("Fen: " + m_Board.GetFENString());
    }
    else if (line == "eval")
    {
        Print("info string " + NeraChessSearch::Evaluation::StatusText());
        Print("info string evaluation " +
            std::to_string(NeraChessSearch::SearchEngine::Evaluate(m_Board)) + " cp");
    }
    else if (line == "quit")
    {
        StopSearch(false);
        return false;
    }
    return true;
}

void UciSession::SetOption(std::string_view command)
{
    const std::vector<std::string> tokens = Tokenize(command);
    const auto nameToken = std::find(tokens.begin(), tokens.end(), "name");
    if (nameToken == tokens.end() || nameToken + 1 == tokens.end())
        return;

    const auto valueToken = std::find(nameToken + 1, tokens.end(), "value");
    const size_t nameBegin = static_cast<size_t>(nameToken - tokens.begin()) + 1;
    const size_t nameEnd = static_cast<size_t>(valueToken - tokens.begin());
    const std::string name = Join(tokens, nameBegin, nameEnd);
    const std::string value = valueToken == tokens.end()
        ? std::string{}
        : Join(tokens, nameEnd + 1, tokens.size());

    StopSearch();
    if (name == "Hash")
    {
        if (const auto megabytes = ParseNumber<int>(value))
            m_SearchEngine.ResizeHash(static_cast<size_t>(std::clamp(*megabytes, 1, 1024)));
    }
    else if (name == "Threads")
    {
        if (const auto threadCount = ParseNumber<int>(value))
        {
            try
            {
                m_SearchEngine.SetThreadCount(
                    static_cast<size_t>(std::clamp(*threadCount, 1, 256)));
            }
            catch (const std::exception& exception)
            {
                Print("info string unable to configure Threads: " +
                    std::string(exception.what()));
            }
        }
    }
    else if (name == "Clear Hash")
    {
        m_SearchEngine.NewGame();
    }
    else if (name == "OwnBook")
    {
        m_OwnBook = value == "true" || value == "1";
    }
    else if (name == "BookFile" && !value.empty())
    {
        m_OpeningBookPath = value;
        m_OpeningBook.Load(m_OpeningBookPath);
    }
    else if (name == "Ponder")
    {
        m_PonderEnabled = value == "true" || value == "1";
    }
    else if (name == "EvalFile")
    {
        // Safe to swap the network here: StopSearch above guarantees no worker
        // is reading it.
        std::string message;
        if (value.empty())
        {
            NeraChessNNUE::Evaluator::Unload();
            m_NetworkPath.clear();
            message = NeraChessSearch::Evaluation::StatusText();
        }
        else if (NeraChessSearch::Evaluation::LoadNetwork(value, message))
        {
            m_NetworkPath = value;
        }
        else
        {
            m_NetworkPath.clear();
        }
        Print("info string " + message);
    }
}

void UciSession::SetPosition(std::string_view command)
{
    StopSearch();
    const std::vector<std::string> tokens = Tokenize(command);
    if (tokens.size() < 2)
        return;

    size_t index = 1;
    NeraChessEngine::ChessBoard candidate;
    if (tokens[index] == "startpos")
    {
        ++index;
    }
    else if (tokens[index] == "fen" && index + 6 < tokens.size())
    {
        std::string fen;
        for (size_t part = 0; part < 6; ++part)
        {
            if (part > 0)
                fen += ' ';
            fen += tokens[++index];
        }
        ++index;
        try
        {
            candidate = NeraChessEngine::ChessBoard(fen);
        }
        catch (...)
        {
            Print("info string invalid FEN");
            return;
        }
        if (candidate.GetError() != 0)
        {
            Print("info string invalid FEN");
            return;
        }
    }
    else
    {
        Print("info string invalid position command");
        return;
    }

    if (index < tokens.size() && tokens[index] == "moves")
    {
        for (++index; index < tokens.size(); ++index)
        {
            const NeraChessEngine::Move move = FindLegalMove(candidate, tokens[index]);
            if (move == 0)
            {
                Print("info string illegal position move " + tokens[index]);
                return;
            }
            candidate.MakeMove(move, true);
        }
    }
    m_Board = std::move(candidate);
}

void UciSession::StartSearch(std::string_view command)
{
    StopSearch();
    const std::vector<std::string> tokens = Tokenize(command);
    NeraChessSearch::SearchLimits limits;
    limits.maxDepth = NeraChessSearch::MAX_PLY - 1;

    std::optional<std::chrono::milliseconds> moveTime;
    std::optional<std::chrono::milliseconds> whiteTime;
    std::optional<std::chrono::milliseconds> blackTime;
    std::chrono::milliseconds whiteIncrement{ 0 };
    std::chrono::milliseconds blackIncrement{ 0 };
    int movesToGo = 0;
    bool infinite = false;
    bool pondering = false;
    bool rootMovesSpecified = false;

    for (size_t index = 1; index < tokens.size(); ++index)
    {
        const std::string& token = tokens[index];
        if (token == "infinite")
        {
            infinite = true;
            continue;
        }
        if (token == "ponder")
        {
            pondering = true;
            continue;
        }
        if (token == "searchmoves")
        {
            rootMovesSpecified = true;
            while (index + 1 < tokens.size())
            {
                const NeraChessEngine::Move move = FindLegalMove(m_Board, tokens[index + 1]);
                if (move == 0)
                    break;
                limits.rootMoves.push_back(move);
                ++index;
            }
            continue;
        }
        if (index + 1 >= tokens.size())
            continue;
        const std::string& value = tokens[index + 1];
        if (token == "depth")
        {
            if (const auto depth = ParseNumber<int>(value))
                limits.maxDepth = std::clamp(*depth, 1, NeraChessSearch::MAX_PLY - 1);
            ++index;
        }
        else if (token == "nodes")
        {
            if (const auto nodes = ParseNumber<uint64_t>(value))
                limits.maxNodes = *nodes;
            ++index;
        }
        else if (token == "mate")
        {
            if (const auto moves = ParseNumber<int>(value))
            {
                // Clamp the mate distance before doubling it: a GUI sending a
                // large but perfectly valid int would otherwise overflow on
                // the multiply, which is undefined behaviour, and land on an
                // arbitrary depth rather than the intended maximum.
                const int plies = std::clamp(*moves, 1, NeraChessSearch::MAX_PLY / 2);
                limits.maxDepth = std::clamp(plies * 2, 1, NeraChessSearch::MAX_PLY - 1);
            }
            ++index;
        }
        else if (token == "movetime")
        {
            if (const auto milliseconds = ParseNumber<int64_t>(value))
                moveTime = std::chrono::milliseconds(std::max<int64_t>(1, *milliseconds));
            ++index;
        }
        else if (token == "wtime" || token == "btime" ||
            token == "winc" || token == "binc")
        {
            if (const auto milliseconds = ParseNumber<int64_t>(value))
            {
                const auto duration = std::chrono::milliseconds(std::max<int64_t>(0, *milliseconds));
                if (token == "wtime") whiteTime = duration;
                if (token == "btime") blackTime = duration;
                if (token == "winc") whiteIncrement = duration;
                if (token == "binc") blackIncrement = duration;
            }
            ++index;
        }
        else if (token == "movestogo")
        {
            if (const auto moves = ParseNumber<int>(value))
                movesToGo = std::max(0, *moves);
            ++index;
        }
    }

    if (rootMovesSpecified && limits.rootMoves.empty())
        limits.rootMoves.push_back(NeraChessEngine::Move(0));

    if (m_OwnBook && !infinite && !pondering)
    {
        const NeraChessEngine::Move bookMove = m_OpeningBook.FindMove(m_Board);
        const bool permitted = !rootMovesSpecified ||
            std::find(limits.rootMoves.begin(), limits.rootMoves.end(), bookMove) !=
                limits.rootMoves.end();
        if (bookMove != 0 && permitted)
        {
            Print("info string opening book move");
            std::string bestMove = "bestmove " + bookMove.ToUCI();
            if (m_PonderEnabled)
            {
                NeraChessEngine::ChessBoard replyPosition = m_Board;
                replyPosition.MakeMove(bookMove, true);
                const NeraChessEngine::Move ponderMove = m_OpeningBook.FindMove(replyPosition);
                if (ponderMove != 0)
                    bestMove += " ponder " + ponderMove.ToUCI();
            }
            Print(bestMove);
            return;
        }
    }

    if (!infinite && moveTime)
    {
        limits.hardTime = *moveTime;
        limits.softTime = std::max(std::chrono::milliseconds{ 1 }, *moveTime * 19 / 20);
    }
    else if (!infinite)
    {
        const bool white = m_Board.GetBoardState().HasFlag(
            NeraChessEngine::BoardStateFlags::WhiteToMove);
        const auto remaining = white ? whiteTime : blackTime;
        if (remaining)
        {
            const auto managed = NeraChessSearch::TimeManagement::CalculateLimits(m_Board,
                *remaining, white ? whiteIncrement : blackIncrement, movesToGo);
            limits.softTime = managed.softTime;
            limits.hardTime = managed.hardTime;
        }
    }

    limits.iterationCallback = [this](const NeraChessSearch::SearchResult& result)
    {
        PrintSearchInfo(result);
    };

    {
        std::scoped_lock lock(m_PonderMutex);
        m_Pondering = pondering;
        m_PonderHit = false;
        m_SuppressBestMove = false;
    }
    m_SearchEngine.PrepareSearch(pondering);
    NeraChessEngine::ChessBoard position = m_Board;
    m_SearchThread = std::jthread([this, position = std::move(position),
        limits = std::move(limits), pondering]() mutable
    {
        NeraChessSearch::SearchResult result;
        try
        {
            result = m_SearchEngine.Search(position, limits);
        }
        catch (const std::exception& exception)
        {
            Print("info string search failed: " + std::string(exception.what()));
            for (const NeraChessEngine::Move move : position.GetLegalMoves())
            {
                result.bestMove = move;
                break;
            }
            result.aborted = true;
        }
        catch (...)
        {
            Print("info string search failed with an unknown error");
            for (const NeraChessEngine::Move move : position.GetLegalMoves())
            {
                result.bestMove = move;
                break;
            }
            result.aborted = true;
        }
        std::unique_lock lock(m_PonderMutex);
        if (pondering)
        {
            m_PonderCondition.wait(lock, [this]
            {
                return m_PonderHit || !m_Pondering;
            });
        }
        if (!m_SuppressBestMove)
            PrintBestMove(result);
        m_Pondering = false;
        m_PonderHit = false;
        m_SuppressBestMove = false;
    });
}

void UciSession::StopSearch(bool emitBestMove)
{
    if (!m_SearchThread.joinable())
        return;
    {
        std::scoped_lock lock(m_PonderMutex);
        m_Pondering = false;
        m_PonderHit = false;
        m_SuppressBestMove = !emitBestMove;
    }
    m_PonderCondition.notify_all();
    m_SearchEngine.RequestStop();
    m_SearchThread.join();
}

void UciSession::PonderHit()
{
    {
        std::scoped_lock lock(m_PonderMutex);
        if (!m_Pondering || m_PonderHit)
            return;
        m_SearchEngine.PonderHit();
        m_PonderHit = true;
    }
    m_PonderCondition.notify_all();
}

void UciSession::PrintBestMove(const NeraChessSearch::SearchResult& result)
{
    std::ostringstream output;
    output << "bestmove " << (result.bestMove == 0 ? "0000" : result.bestMove.ToUCI());
    if (m_PonderEnabled && result.principalVariation.size() > 1)
        output << " ponder " << result.principalVariation[1].ToUCI();
    Print(output.str());
}

void UciSession::PrintSearchInfo(const NeraChessSearch::SearchResult& result)
{
    std::ostringstream output;
    output << "info depth " << result.completedDepth
           << " seldepth " << result.selectiveDepth;
    if (std::abs(result.score) >= NeraChessSearch::SCORE_MATE - NeraChessSearch::MAX_PLY)
    {
        const int plies = NeraChessSearch::SCORE_MATE - std::abs(result.score);
        const int moves = (plies + 1) / 2;
        output << " score mate " << (result.score >= 0 ? moves : -moves);
    }
    else
    {
        output << " score cp " << result.score;
    }
    const int64_t elapsed = result.elapsed.count();
    const uint64_t nps = elapsed > 0 ? result.nodes * 1000 / elapsed : result.nodes * 1000;
    output << " nodes " << result.nodes
           << " nps " << nps
           << " hashfull " << result.hashFullPermill
           << " time " << elapsed;
    if (!result.principalVariation.empty())
    {
        output << " pv";
        for (const NeraChessEngine::Move move : result.principalVariation)
            output << ' ' << move.ToUCI();
    }
    Print(output.str());
}

void UciSession::Print(std::string_view line)
{
    std::scoped_lock lock(m_OutputMutex);
    m_Output << line << '\n' << std::flush;
}
