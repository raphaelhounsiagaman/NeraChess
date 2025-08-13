#include "Application.h"

#include "NeraChessCore.h"

#include "InputHandler.h"
#include "Renderer.h"

#include "MoveGenerator.h"

Application::Application()
{
	m_Running = true;

	m_Renderer.SetChessBoard(&m_ChessBoard);
	m_Renderer.SetInputHandler(m_InputHandler);

	if (m_Renderer.GetError()) 
		m_Running = false;
}

void Application::Run()
{	
	Bitboard showBoard = 0ULL;
	int selected = 0;

	std::vector<Move> moves = m_ChessBoard.GetLegalMoves();

	while (m_Running)
	{
		m_InputHandler.Process();


		InputEvent event;
		while (m_InputHandler.PollInputEvent(&event))
		{
			switch (event.type)
			{
				case EventTypeQuit:
					m_Running = false;
					break;
				case EventTypeWindowResize:
					m_Renderer.UpdateWindowSize();
					break;
				case EventTypeKeyPressed:
					selected++;
					if (selected > 63)
						selected = 0;
					showBoard = 0;

					for (Move move : moves)
					{
						if (move.startSquare == selected)
						{
							showBoard |= (1ULL << move.targetSquare);
						}
					}

					break;
				default:
					break;

			}
		}

		m_Renderer.SetBitboard(showBoard);
		m_Renderer.Render();
	}
}
