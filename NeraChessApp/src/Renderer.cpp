#include "Renderer.h"

#include "ChessBoard.h"

#include <iostream>
#include <cstdint>
#include <string>

#include "SDL.h"
#include "SDL_image.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "InputHandler.h"

Renderer::Renderer()
{
    InitSDL();
    InitDearImGui();
    InitPieceRects();
}

Renderer::~Renderer()
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(m_SDLRenderer);
    SDL_DestroyWindow(m_SDLWindow);
    IMG_Quit();
    SDL_Quit();
}

void Renderer::InitSDL()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS))
	{
		printf("Error: SDL_Init(): %s\n", SDL_GetError());
		m_Error = 1;
	}

	m_MainScale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);

    if (IMG_Init(IMG_INIT_PNG) == 0)
    {
        printf("Error: IMG_Init(): %s\n", SDL_GetError());
        m_Error = 1;
    }

	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    m_SDLWindow = SDL_CreateWindow(m_WindowName.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(1280 * m_MainScale), (int)(720 * m_MainScale), windowFlags);
	if (m_SDLWindow == nullptr)
	{
		printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		m_Error = 1;
	}

    m_SDLRenderer = SDL_CreateRenderer(m_SDLWindow, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (m_SDLRenderer == nullptr)
    {
        printf("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        m_Error = 1;
    }

    SDL_SetWindowMinimumSize(m_SDLWindow, 720, 480);
    UpdateWindowSize();   
}

void Renderer::InitDearImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_IO = &ImGui::GetIO(); 
    
    m_IO->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     
    m_IO->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      
    m_IO->ConfigFlags |= ImGuiConfigFlags_DockingEnable;         

    ImGui::StyleColorsDark();

    m_Style = &ImGui::GetStyle();
    m_Style->ScaleAllSizes(m_MainScale);        
    m_Style->FontScaleDpi = m_MainScale;
    m_IO->ConfigDpiScaleViewports = true;

    ImGui_ImplSDL2_InitForSDLRenderer(m_SDLWindow, m_SDLRenderer);
    ImGui_ImplSDLRenderer2_Init(m_SDLRenderer);
}

void Renderer::InitPieceRects()
{
	// Assuming the chess piece sprites are arranged in a 6x2 grid:
	// White pieces: K Q B N R P
	// Black pieces: k q b n r p

    m_ChessSprites = IMG_LoadTexture(m_SDLRenderer, "Ressources/Sprites/ChessPieces.png");
    if (m_ChessSprites == nullptr)
    {
        printf("Error: IMG_LoadTexture(): %s\n", SDL_GetError());
        m_Error = 1;
    }

    int textureWidth, textureHeight;
    SDL_QueryTexture(m_ChessSprites, nullptr, nullptr, &textureWidth, &textureHeight);
    int pieceWidth = textureWidth / 6;
    int pieceHeight = textureHeight / 2;

    m_WhiteKing = { pieceWidth * 0, 0, pieceWidth, pieceHeight };
    m_WhiteQueen = { pieceWidth * 1, 0, pieceWidth, pieceHeight };
    m_WhiteBishop = { pieceWidth * 2, 0, pieceWidth, pieceHeight };
    m_WhiteKnight = { pieceWidth * 3, 0, pieceWidth, pieceHeight };
    m_WhiteRook = { pieceWidth * 4, 0, pieceWidth, pieceHeight };
    m_WhitePawn = { pieceWidth * 5, 0, pieceWidth, pieceHeight };
    m_BlackKing = { pieceWidth * 0, pieceHeight, pieceWidth, pieceHeight };
    m_BlackQueen = { pieceWidth * 1, pieceHeight, pieceWidth, pieceHeight };
    m_BlackBishop = { pieceWidth * 2, pieceHeight, pieceWidth, pieceHeight };
    m_BlackKnight = { pieceWidth * 3, pieceHeight, pieceWidth, pieceHeight };
    m_BlackRook = { pieceWidth * 4, pieceHeight, pieceWidth, pieceHeight };
    m_BlackPawn = { pieceWidth * 5, pieceHeight, pieceWidth, pieceHeight };
}

void Renderer::Render(const ChessBoard& board)
{
    if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED)
    {
        SDL_Delay(10);
        return;
    }
    
    DrawChessBoard(board);
    DrawUI();

    SDL_RenderPresent(m_SDLRenderer);

    SDL_SetRenderDrawColor(m_SDLRenderer, m_BackgroundColor.r, m_BackgroundColor.g, m_BackgroundColor.b, m_BackgroundColor.a);
    SDL_RenderClear(m_SDLRenderer);
}

void Renderer::SetInputHandler(InputHandler& inputHandler)
{
    m_InputHandler = &inputHandler;
    m_InputHandler->SetImGuiIO(m_IO);
}

void Renderer::DrawChessBoard(const ChessBoard& board)
{
    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            (rank + file) % 2 != 0 ?
                SDL_SetRenderDrawColor(m_SDLRenderer, m_BoardWhite.r, m_BoardWhite.g, m_BoardWhite.b, m_BoardWhite.a) :
                SDL_SetRenderDrawColor(m_SDLRenderer, m_BoardBlack.r, m_BoardBlack.g, m_BoardBlack.b, m_BoardBlack.a);

            if (m_WhiteBottom)
            {
                if (m_DebugBitboard && ((m_DebugBitboard >> (rank * 8 + file)) & 1U))
                    (rank + file) % 2 != 0 ?
                    SDL_SetRenderDrawColor(m_SDLRenderer, m_DebugWhite.r, m_DebugWhite.g, m_DebugWhite.b, m_DebugWhite.a) :
                    SDL_SetRenderDrawColor(m_SDLRenderer, m_DebugBlack.r, m_DebugBlack.g, m_DebugBlack.b, m_DebugBlack.a);
            }
            else
            {
                if (m_DebugBitboard && ((m_DebugBitboard >> ((7 - rank) * 8 + 7 - file)) & 1U))
                    ((7 - rank)+ 7 - file) % 2 != 0 ?
                    SDL_SetRenderDrawColor(m_SDLRenderer, m_DebugWhite.r, m_DebugWhite.g, m_DebugWhite.b, m_DebugWhite.a) :
                    SDL_SetRenderDrawColor(m_SDLRenderer, m_DebugBlack.r, m_DebugBlack.g, m_DebugBlack.b, m_DebugBlack.a);
            }
            
            SDL_Rect tile = { file * m_TileSize + m_Margin, (7 - rank) * m_TileSize + m_Margin, m_TileSize, m_TileSize };
            SDL_RenderFillRect(m_SDLRenderer, &tile);

            Piece piece;
            if (m_WhiteBottom)
                piece = board.GetPiece(rank * 8 + file);
            else
                piece = board.GetPiece((7 - rank) * 8 + 7 - file);

            if (piece == PieceType::NO_PIECE)
                continue;

            SDL_Rect source;

            source =
                piece == (uint8_t)PieceType::WHITE_PAWN ?
                m_WhitePawn :
                piece == (uint8_t)PieceType::WHITE_KNIGHT ?
                m_WhiteKnight :
                piece == (uint8_t)PieceType::WHITE_BISHOP ?
                m_WhiteBishop :
                piece == (uint8_t)PieceType::WHITE_ROOK ?
                m_WhiteRook :
                piece == (uint8_t)PieceType::WHITE_QUEEN ?
                m_WhiteQueen :
                piece == (uint8_t)PieceType::WHITE_KING ?
                m_WhiteKing :
                piece == (uint8_t)PieceType::BLACK_PAWN ?
                m_BlackPawn :
                piece == (uint8_t)PieceType::BLACK_KNIGHT ?
                m_BlackKnight :
                piece == (uint8_t)PieceType::BLACK_BISHOP ?
                m_BlackBishop :
                piece == (uint8_t)PieceType::BLACK_ROOK ?
                m_BlackRook :
                piece == (uint8_t)PieceType::BLACK_QUEEN ?
                m_BlackQueen :
                piece == (uint8_t)PieceType::BLACK_KING ?
                m_BlackKing :
                SDL_Rect{ 0, 0, 0, 0};

            SDL_RenderCopy(m_SDLRenderer, m_ChessSprites, &source, &tile);
        }
    }
}

void Renderer::DrawUI()
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

	bool show_demo_window = true;
    //ImGui::ShowDemoWindow(&show_demo_window);

    ImGuiWindow();

    ImGui::Render();
    SDL_RenderSetScale(m_SDLRenderer, m_IO->DisplayFramebufferScale.x, m_IO->DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_SDLRenderer);
}

void Renderer::ImGuiWindow()
{
    ImGui::Begin("Settings");

	if(ImGui::Button("Start Game"))
		m_InputHandler->AddInputEvent(InputEvent(EventTypeStartGame));

    ImGui::End();
}

uint8_t Renderer::GetSquareFromPos(int x, int y) const
{
    bool isOutOfBounds = 
        x < m_Margin ||
        x > m_Margin + 8 * m_TileSize ||
        y < m_Margin ||
        y > m_Margin + 8 * m_TileSize;
    if (isOutOfBounds)
		return 64;

    uint8_t file = (x - m_Margin) / m_TileSize;
    uint8_t rank = 7 - ((y - m_Margin) / m_TileSize);

    uint8_t square = 64;

    if (m_WhiteBottom)
	    square = rank * 8 + file;
    else 
        square = (7 - rank) * 8 + 7 - file;

    return square;
}

void Renderer::UpdateWindowSize()
{
    int windowWidth = 0, windowHeight = 0;

    SDL_GetWindowSize(m_SDLWindow, &windowWidth, &windowHeight);

    bool isHorizontalWindow = windowWidth > windowHeight;

    m_Margin = (int)(m_MarginPortion * (isHorizontalWindow ? windowHeight : windowWidth));

    m_TileSize = isHorizontalWindow ? (windowHeight - 2 * m_Margin) / 8 : (windowWidth - 2 * m_Margin) / 8;
}
