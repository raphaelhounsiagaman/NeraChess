#pragma once

#include "Core/Layer.h"
#include "Core/InputEvents.h"
#include "Core/WindowEvents.h"

#include "Core/Renderer/Renderer.h"
#include "Core/Sound/SoundPlayer.h"
#include "Core/Math/Vec2.h"

#include "ChessBoard.h"

#include <array>
#include <vector>
#include <functional>
#include <filesystem>


class BoardLayer : public NeraCore::Layer
{ 
public:
	BoardLayer();
	virtual ~BoardLayer();

	virtual void OnEvent(NeraCore::Event& event) override;

	virtual void OnUpdate(float deltaTime) override;
	virtual void OnRender() override;

	void PlayMove(ChessCore::Move move);

	void SetWhiteBottom(bool whiteBottom) { m_WhiteBottom = whiteBottom;  }
	void SetChessBoard(const ChessCore::ChessBoard& board = ChessCore::ChessBoard()) { m_ChessBoard = board; };
	void SetMovePtr(ChessCore::Move* move);

private:
	bool OnMouseButtonPressed(NeraCore::MouseButtonPressedEvent& event);
	bool OnMouseButtonReleased(NeraCore::MouseButtonReleasedEvent& event);
	bool OnMouseMoved(NeraCore::MouseMovedEvent& event);
	bool OnWindowResize(NeraCore::WindowResizeEvent& event);

	void DrawBoard();
	void DrawHighlights();
	void DrawAnimatedPiece();
	void DrawPieces();
	void DrawFlyingPiece();

	void TryMakeMove(ChessCore::Move move);
	static void AddSoundsToList(std::filesystem::path path, std::vector<NeraCore::Sound>& list);
	void PlayRandomSoundFromList(const std::vector<NeraCore::Sound>& sounds);

	void UpdateSize(NeraCore::Vec2<uint32_t> windowSize);

private:

	NeraCore::Renderer& m_Renderer;
	NeraCore::SoundPlayer& m_SoundPlayer;

	// Piece Drawing

	NeraCore::Texture m_Texture;
	std::array<NeraCore::Sprite, 12> m_PieceSprites;


	// Board Drawing

	ChessCore::ChessBoard m_ChessBoard = ChessCore::ChessBoard();

	NeraCore::Color m_LightSquareColor = NeraCore::Color(217, 199, 156);
	NeraCore::Color m_DarkSquareColor = NeraCore::Color(145, 88, 32);

	float m_MarginProportion = 0.05f;
	NeraCore::Vec2<uint32_t> m_Margin{ 0, 0 };
	NeraCore::Vec2<uint32_t> m_SquareSize{ 1, 1 };

	// Highlight Drawing

	ChessCore::Move m_LastMovePlayed = 0;
	NeraCore::Color m_LastMoveColor{ 181, 79, 45, 128 };

	ChessCore::Bitboard m_MarkedSquares = 0;
	NeraCore::Color m_MarkedSquareColor{ 184, 91, 70, 128 };

	ChessCore::Piece m_SelectedPiece = ChessCore::PieceType::NO_PIECE;
	ChessCore::Square m_SelectedPieceSquare = 64;
	NeraCore::Color m_SelectedPieceColor{ 191, 92, 59, 128 };

	ChessCore::MoveList<218> m_LegalMoves;

	// Animated Piece Drawing

	bool m_AnimationSkipped = false;
	float m_AnimationsLengthS = 0.3f;
	float m_AnimationDone = 0.f;
	ChessCore::Move m_AnimationMove = 0;
	ChessCore::Piece m_AnimationPiece = ChessCore::PieceType::NO_PIECE;
	ChessCore::Square m_AnimationFromSquare = 64;

	// FlyingPiece Drawing

	ChessCore::Piece m_FlyingPiece = ChessCore::PieceType::NO_PIECE;
	ChessCore::Square m_FlyingPieceSquare = 64;

	// Sounds

	std::vector<NeraCore::Sound> m_MoveSounds;
	std::vector<NeraCore::Sound> m_CaptureSounds;

	// Misc 

	NeraCore::Vec2<uint32_t> m_MousePosition{ 0, 0 };
	ChessCore::Move* m_MovePtr = nullptr;
	bool m_WhiteBottom = true;

	
};
