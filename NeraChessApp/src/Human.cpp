#include "Human.h"

#include <vector>

Move Human::GetNextMove(const ChessBoard& board)
{
	MoveList legalMoves = board.GetLegalMoves();

	uint8_t startSquare = 64;
	uint8_t targetSquare = 64;

    while (true)
    {
        std::lock_guard<std::mutex> lock(m_SelectedSquareMutex);
        if (m_SelectedSquare != 64)
        {
            m_PossibleMoves = 0ULL;

            if (startSquare == 64)
            {
                startSquare = m_SelectedSquare;
            }
            else
            {
				targetSquare = m_SelectedSquare;
            }

            m_SelectedSquare = 64;
        }

        if (startSquare != 64 && targetSquare != 64)
        {
            m_PossibleMoves = 0ULL;

            for (Move legalMove : legalMoves)
            {
                if (MoveUtil::GetFromSquare(legalMove) == startSquare && MoveUtil::GetTargetSquare(legalMove) == targetSquare)
                {
                    return legalMove;
                }
            }

            startSquare = 64;
            targetSquare = 64;

		}
        if (startSquare != 64 && targetSquare == 64)
        {
            if (!m_PossibleMoves)
            {
                for (Move legalMove : legalMoves)
                {
                    if (MoveUtil::GetFromSquare(legalMove) == startSquare)
                    {
                        std::lock_guard<std::mutex> lock(m_PossibleMovesMutex);
                        m_PossibleMoves |= 1ULL << MoveUtil::GetTargetSquare(legalMove);
                    }
                }

                if (m_PossibleMoves == 0ULL)
                {
                    startSquare = 64;
				}
            }

        }




    }
    return 0;
}
