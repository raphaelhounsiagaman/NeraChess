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


class BoardLayer : public ApplicationCore::Layer
{ 
public:
	BoardLayer();
	virtual ~BoardLayer();

	virtual void OnEvent(ApplicationCore::Event& event) override;

	virtual void OnUpdate(float deltaTime) override;
	virtual void OnRender() override;

	void PlayMove(NeraChessEngine::Move move);

	void SetWhiteBottom(bool whiteBottom) { m_WhiteBottom = whiteBottom;  }
	void SetChessBoard(const NeraChessEngine::ChessBoard& board = NeraChessEngine::ChessBoard()) { m_ChessBoard = board; };
	void SetMovePtr(NeraChessEngine::Move* move);

private:
	bool OnMouseButtonPressed(ApplicationCore::MouseButtonPressedEvent& event);
	bool OnMouseButtonReleased(ApplicationCore::MouseButtonReleasedEvent& event);
	bool OnMouseMoved(ApplicationCore::MouseMovedEvent& event);
	bool OnWindowResize(ApplicationCore::WindowResizeEvent& event);

	void DrawBoard();
	void DrawHighlights();
	void DrawAnimatedPiece();
	void DrawPieces();
	void DrawFlyingPiece();

	void TryMakeMove(NeraChessEngine::Move move);
	static void AddSoundsToList(std::filesystem::path path, std::vector<ApplicationCore::Sound>& list);
	void PlayRandomSoundFromList(const std::vector<ApplicationCore::Sound>& sounds);

	void UpdateSize(ApplicationCore::Vec2<uint32_t> windowSize);

private:

	ApplicationCore::Renderer& m_Renderer;
	ApplicationCore::SoundPlayer& m_SoundPlayer;

	// Piece Drawing

	ApplicationCore::Texture m_Texture;
	std::array<ApplicationCore::Sprite, 12> m_PieceSprites;


	// Board Drawing

	NeraChessEngine::ChessBoard m_ChessBoard = NeraChessEngine::ChessBoard();

	ApplicationCore::Color m_LightSquareColor = ApplicationCore::Color(217, 199, 156);
	ApplicationCore::Color m_DarkSquareColor = ApplicationCore::Color(145, 88, 32);

	float m_MarginProportion = 0.05f;
	ApplicationCore::Vec2<uint32_t> m_Margin{ 0, 0 };
	ApplicationCore::Vec2<uint32_t> m_SquareSize{ 1, 1 };

	// Highlight Drawing

	NeraChessEngine::Move m_LastMovePlayed = 0;
	ApplicationCore::Color m_LastMoveColor{ 181, 79, 45, 128 };

	NeraChessEngine::Bitboard m_MarkedSquares = 0;
	ApplicationCore::Color m_MarkedSquareColor{ 184, 91, 70, 128 };

	NeraChessEngine::Piece m_SelectedPiece = NeraChessEngine::PieceType::NO_PIECE;
	NeraChessEngine::Square m_SelectedPieceSquare = 64;
	ApplicationCore::Color m_SelectedPieceColor{ 191, 92, 59, 128 };

	NeraChessEngine::MoveList<218> m_LegalMoves;

	// Animated Piece Drawing

	bool m_AnimationSkipped = false;
	float m_AnimationsLengthS = 0.3f;
	float m_AnimationDone = 0.f;
	NeraChessEngine::Move m_AnimationMove = 0;
	NeraChessEngine::Piece m_AnimationPiece = NeraChessEngine::PieceType::NO_PIECE;
	NeraChessEngine::Square m_AnimationFromSquare = 64;

	// FlyingPiece Drawing

	NeraChessEngine::Piece m_FlyingPiece = NeraChessEngine::PieceType::NO_PIECE;
	NeraChessEngine::Square m_FlyingPieceSquare = 64;

	// Sounds

	std::vector<ApplicationCore::Sound> m_MoveSounds;
	std::vector<ApplicationCore::Sound> m_CaptureSounds;

	// Misc 

	ApplicationCore::Vec2<uint32_t> m_MousePosition{ 0, 0 };
	NeraChessEngine::Move* m_MovePtr = nullptr;
	bool m_WhiteBottom = true;

	
};
