#pragma once

#include "ChessPlayer.h"

class MyBotOld : public ChessPlayer
{
public:
	
	Move GetNextMove(const ChessBoard& board) override;

};
