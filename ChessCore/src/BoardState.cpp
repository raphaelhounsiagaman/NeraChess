#include "BoardState.h"

namespace ChessCore
{
	bool BoardState::operator==(const BoardState& other) const
	{
		bool same = true;

		if (pieceBitboards != other.pieceBitboards)
			same = false;
		if (boardStateFlags != other.boardStateFlags)
			same = false;
		if (boardStateFlags & BoardStateFlags::CanEnPassent && enPassantFile != other.enPassantFile)
			same = false;

		return same;
	}

} // namespace ChessCore
