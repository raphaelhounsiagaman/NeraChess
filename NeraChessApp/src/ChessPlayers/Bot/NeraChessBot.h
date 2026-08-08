#pragma once

#include "../ChessPlayer.h"
#include "OpeningBook.h"
#include "SearchEngine.h"

#include <filesystem>

class NeraChessBot : public ChessPlayer
{
public:
    NeraChessBot();

    NeraChessEngine::Move GetNextMove(const NeraChessEngine::ChessBoard& board,
        const NeraChessEngine::Clock& timer) override;
    void ResetGame() override;
    void StopSearching() override;

private:
    const std::filesystem::path m_OpeningBookPath;
    NeraChessSearch::OpeningBook m_OpeningBook;
    NeraChessSearch::SearchEngine m_SearchEngine{ 256 };
};
