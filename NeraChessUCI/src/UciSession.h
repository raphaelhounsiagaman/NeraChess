#pragma once

#include "ChessBoard.h"
#include "OpeningBook.h"
#include "SearchEngine.h"

#include <condition_variable>
#include <filesystem>
#include <istream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>

class UciSession
{
public:
    UciSession(std::istream& input, std::ostream& output,
        std::filesystem::path openingBookPath = {},
        std::filesystem::path executableDirectory = {});
    ~UciSession();

    int Run();

private:
    bool HandleCommand(const std::string& line);
    void SetPosition(std::string_view command);
    void SetOption(std::string_view command);
    void StartSearch(std::string_view command);
    void StopSearch(bool emitBestMove = true);
    void PonderHit();
    void PrintBestMove(const NeraChessSearch::SearchResult& result);
    void PrintSearchInfo(const NeraChessSearch::SearchResult& result);
    void Print(std::string_view line);

private:
    std::istream& m_Input;
    std::ostream& m_Output;
    std::mutex m_OutputMutex;
    std::mutex m_PonderMutex;
    std::condition_variable m_PonderCondition;
    NeraChessEngine::ChessBoard m_Board;
    NeraChessSearch::SearchEngine m_SearchEngine{ 64 };
    std::filesystem::path m_OpeningBookPath;
    std::filesystem::path m_ExecutableDirectory;
    std::filesystem::path m_NetworkPath;
    NeraChessSearch::OpeningBook m_OpeningBook;
    bool m_OwnBook = true;
    bool m_PonderEnabled = false;
    bool m_Pondering = false;
    bool m_PonderHit = false;
    bool m_SuppressBestMove = false;
    std::jthread m_SearchThread;
};
