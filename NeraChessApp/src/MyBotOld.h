#pragma once

#include "ChessPlayer.h"

#include "NeraChessCore.h"

class MyBotOld : public ChessPlayer
{
public:
	Move GetNextMove(const ChessBoard& board) override;
};
