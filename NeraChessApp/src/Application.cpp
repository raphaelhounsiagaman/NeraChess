#include "Application.h"

#include "NeraChessCore.h"

#include "InputHandler.h"
#include "Renderer.h"

#include "MoveGenerator.h"

#include <iostream>



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

	//rnb1kbnr/pppqpppp/3p4/8/Q7/2P5/PP1PPPPP/RNB1KBNR w KQkq - 2 3

	ChessBoard::RunPerformanceTest(ChessBoard(), 5);

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
					break;
				default:
					break;

			}
		}

		m_Renderer.SetBitboard(showBoard);
		m_Renderer.Render();
	}
}


