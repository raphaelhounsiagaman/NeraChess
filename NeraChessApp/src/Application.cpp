#include "Application.h"

#include "NeraChessCore.h"

#include "InputHandler.h"
#include "Renderer.h"

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
				default:
					break;

			}
		}


		m_Renderer.Render();
	}
}
