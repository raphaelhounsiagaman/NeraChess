#pragma once

#include "../ChessPlayer.h"
#include "SearchEngine.h"

#include <filesystem>
#include <fstream>

class NeraChessBot : public ChessPlayer
{
public:
    NeraChessBot();

    NeraChessEngine::Move GetNextMove(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Clock& timer) override;
    void ResetGame() override;
    void StopSearching() override;

private:
    NeraChessEngine::Move GetOpeningBookMove(const NeraChessEngine::ChessBoard& board);

private:
    const std::filesystem::path m_OpeningBookPath;
    std::ifstream m_OpeningBook;
    bool m_OpeningBookAvailable = true;
    NeraChessSearch::SearchEngine m_SearchEngine{ 256 };
};
