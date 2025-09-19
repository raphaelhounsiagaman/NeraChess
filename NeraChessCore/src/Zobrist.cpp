#include "Zobrist.h"

std::mt19937_64 Zobrist::rng(std::random_device{}());

template<size_t Size>
std::array<uint64_t, Size> Zobrist::GetRandomArray()
{
	std::array<uint64_t, Size> arr{};
	for (size_t i = 0; i < Size; i++)
	{
		arr[i] = rng();
	}
	return arr;
}

template<size_t SizeX, size_t SizeY>
std::array<std::array<uint64_t, SizeY>, SizeX> Zobrist::GetRandom2DArray()
{
    std::array<std::array<uint64_t, SizeY>, SizeX> arr{};
    for (size_t i = 0; i < SizeX; i++)
    {
        arr[i] = GetRandomArray<SizeY>();
    }
    return arr;
}

const std::array<std::array<uint64_t, 64>, 12> Zobrist::piecesArray = GetRandom2DArray<12, 64>();
// Each player has 4 possible castling right states: none, queenside, kingside, both.
// So, taking both sides into account, there are 16 possible states.
const std::array<uint64_t, 16> Zobrist::castlingRights = GetRandomArray<16>();
// En passant file (0 = no ep).
//  Rank does not need to be specified since side to move is included in key
const std::array<uint64_t, 9> Zobrist::enPassantFile = GetRandomArray<9>();
const uint64_t Zobrist::sideToMove = rng();

uint64_t Zobrist::CalculateZobristKey(ChessBoard board)
{
    uint64_t zobristKey = 0;

    for (int squareIndex = 0; squareIndex < 64; squareIndex++)
    {
        int piece = board.GetPiece(squareIndex);

        if (piece != PieceType::NO_PIECE)
        {
            zobristKey ^= piecesArray[piece][squareIndex];
        }
    }

    zobristKey ^= enPassantFile[board.GetBoardState().enPassantFile];

    if (!board.GetBoardState().HasFlag(BoardStateFlags::WhiteToMove))
    {
        zobristKey ^= sideToMove;
    }

    zobristKey ^= castlingRights[board.GetBoardState().GetCastlingRights()];

    return zobristKey;
}
