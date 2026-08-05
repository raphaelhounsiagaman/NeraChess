#pragma once

#include "ChessBoard.h"
#include "OpeningBook.h"
#include "SearchEngine.h"

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
        std::filesystem::path openingBookPath = {});
    ~UciSession();

    int Run();

private:
    bool HandleCommand(const std::string& line);
    void SetPosition(std::string_view command);
    void SetOption(std::string_view command);
    void StartSearch(std::string_view command);
    void StopSearch();
    void PrintSearchInfo(const NeraChessSearch::SearchResult& result);
    void Print(std::string_view line);

private:
    std::istream& m_Input;
    std::ostream& m_Output;
    std::mutex m_OutputMutex;
    NeraChessEngine::ChessBoard m_Board;
    NeraChessSearch::SearchEngine m_SearchEngine{ 64 };
    std::filesystem::path m_OpeningBookPath;
    NeraChessSearch::OpeningBook m_OpeningBook;
    bool m_OwnBook = true;
    std::jthread m_SearchThread;
};
