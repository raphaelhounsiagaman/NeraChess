#pragma once

#include "ChessBoard.h"

#include <cstdint>
#include <string>

#include "SDL.h"
#include "imgui.h"

#include "InputHandler.h"

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Render(const ChessBoard& board);
	
	void SetWhiteBottom(bool whiteBottom) { m_WhiteBottom = whiteBottom; }
	void SetInputHandler(InputHandler& inputHandler);
	void SetBitboard(Bitboard board) { m_DebugBitboard = board; }

	uint8_t GetSquareFromPos(int x, int y) const;
	uint8_t GetError() const { return m_Error; }

	void UpdateWindowSize();

private:

	void InitSDL();
	void InitDearImGui();
	void InitPieceRects();

	void DrawUI();
	void DrawChessBoard(const ChessBoard& board);

	void ImGuiWindow();

private:

	const std::string m_WindowName = "NeraChess";

	const SDL_Color m_BackgroundColor = { 45, 45, 50, 255 };
	const SDL_Color m_BoardWhite = { 210, 200, 180, 255 };
	const SDL_Color m_BoardBlack = { 150, 120, 90, 255 };
		
	const SDL_Color m_DebugWhite = { 161, 96, 96, 255 };
	const SDL_Color m_DebugBlack = { 87, 36, 36, 255 };

	const float m_MarginPortion = 0.05f;

	bool m_WhiteBottom = true;

	Bitboard m_DebugBitboard = 0ULL;
	InputHandler* m_InputHandler = nullptr;

	int m_TileSize = 0;
	int m_Margin = 0;
	
	uint8_t m_Error = 0;

	float m_MainScale = 1.f;

	SDL_Window* m_SDLWindow = nullptr;
	SDL_Renderer* m_SDLRenderer = nullptr;

	SDL_Texture* m_ChessSprites = nullptr;

	ImGuiIO* m_IO = nullptr;
	ImGuiStyle* m_Style = nullptr;

	SDL_Rect m_WhiteKing = {};
	SDL_Rect m_WhiteQueen = {};
	SDL_Rect m_WhiteBishop = {};
	SDL_Rect m_WhiteKnight = {};
	SDL_Rect m_WhiteRook = {};
	SDL_Rect m_WhitePawn = {};
	SDL_Rect m_BlackKing = {};
	SDL_Rect m_BlackQueen = {};
	SDL_Rect m_BlackBishop = {};
	SDL_Rect m_BlackKnight = {};
	SDL_Rect m_BlackRook = {};
	SDL_Rect m_BlackPawn = {};
};