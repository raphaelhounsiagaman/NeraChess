#include "BoardState.h"

namespace NeraChessEngine
{
	bool BoardState::operator==(const BoardState& other) const
	{
		bool same = true;

		if (pieceBitboards != other.pieceBitboards)
			same = false;
		if (boardStateFlags != other.boardStateFlags)
			same = false;
		if (boardStateFlags & BoardStateFlags::CanEnPassant && enPassantFile != other.enPassantFile)
			same = false;

		return same;
	}

} // namespace NeraChessEngine