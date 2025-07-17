#pragma once

#include "NeraChessCore.h"

#include "InputHandler.h"
#include "Renderer.h"

class Application
{
public:
	Application();
	~Application() = default;

	void Run();

private:

	bool m_Running = false;

	InputHandler m_InputHandler{};
	Renderer m_Renderer{};

	ChessBoard m_ChessBoard{};

};