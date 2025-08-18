#include "BoardState.h"

bool BoardState::operator==(const BoardState& other) const
{
	bool same = true;

	if (pieceBitboards != other.pieceBitboards)
		same = false;
	if (boardStateFlags != other.boardStateFlags)
		same = false;
	if (boardStateFlags & BoardStateFlags::CanEnPassent && enPassentFile != other.enPassentFile)
		same = false;

    return same;
}
