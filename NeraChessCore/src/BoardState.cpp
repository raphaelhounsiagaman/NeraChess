#include "BoardState.h"

bool BoardState::operator==(const BoardState& other) const
{
	bool same = true;

	if (pieceBitboards != other.pieceBitboards)
		same = false;
	if (boardStateFlags != other.boardStateFlags)
		same = false;
	if (0 != (boardStateFlags & (uint8_t)BoardStateFlags::CanEnPassent) && enPassentFile != other.enPassentFile)
		same = false;

	/*
    if (
		pieceBitboards == other.pieceBitboards &&
		boardStateFlags == other.boardStateFlags &&
		enPassentFile == other.enPassentFile
        )
		return true;
	*/


    return same;
}
