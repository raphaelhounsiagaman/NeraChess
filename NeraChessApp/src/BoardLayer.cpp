#include "BoardLayer.h"

#include "Core/Application.h"
#include "Core/Event.h"
#include "Resources.h"
#include "UILayout.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

BoardLayer::BoardLayer()
	: m_Renderer(ApplicationCore::Application::Get().GetWindow()->GetRenderer()),
	  m_SoundPlayer(ApplicationCore::Application::Get().GetWindow()->GetSoundPlayer()),
	  m_Texture(NeraChessApp::GetResourcePath("Sprites/ChessPieces.png").string()),
	  m_PieceSprites(
		  {ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture},
		   ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture},
		   ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture},
		   ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture}, ApplicationCore::Sprite{m_Texture}})
{
	UpdateSize(ApplicationCore::Application::Get().GetWindow()->GetSize());

	uint32_t pieceWidth = m_Texture.GetSize().X / 6;
	uint32_t pieceHeight = m_Texture.GetSize().Y / 2;

	for (NeraChessEngine::Piece piece = 0; piece < 12; piece++)
	{
		m_PieceSprites[piece].Position.Y = piece < 6 ? 0 : pieceHeight;
		m_PieceSprites[piece].Size = {pieceWidth, pieceHeight};
	}

	m_PieceSprites[NeraChessEngine::WHITE_KING].Position.X = pieceWidth * 0;
	m_PieceSprites[NeraChessEngine::WHITE_QUEEN].Position.X = pieceWidth * 1;
	m_PieceSprites[NeraChessEngine::WHITE_BISHOP].Position.X = pieceWidth * 2;
	m_PieceSprites[NeraChessEngine::WHITE_KNIGHT].Position.X = pieceWidth * 3;
	m_PieceSprites[NeraChessEngine::WHITE_ROOK].Position.X = pieceWidth * 4;
	m_PieceSprites[NeraChessEngine::WHITE_PAWN].Position.X = pieceWidth * 5;
	m_PieceSprites[NeraChessEngine::BLACK_KING].Position.X = pieceWidth * 0;
	m_PieceSprites[NeraChessEngine::BLACK_QUEEN].Position.X = pieceWidth * 1;
	m_PieceSprites[NeraChessEngine::BLACK_BISHOP].Position.X = pieceWidth * 2;
	m_PieceSprites[NeraChessEngine::BLACK_KNIGHT].Position.X = pieceWidth * 3;
	m_PieceSprites[NeraChessEngine::BLACK_ROOK].Position.X = pieceWidth * 4;
	m_PieceSprites[NeraChessEngine::BLACK_PAWN].Position.X = pieceWidth * 5;

	AddSoundsToList(NeraChessApp::GetResourcePath("Sounds/Moves"), m_MoveSounds);
	AddSoundsToList(NeraChessApp::GetResourcePath("Sounds/Captures"), m_CaptureSounds);
}

void BoardLayer::OnEvent(ApplicationCore::Event& event)
{
	ApplicationCore::EventDispatcher dispatcher(event);

	dispatcher.Dispatch<ApplicationCore::WindowResizeEvent>([this](ApplicationCore::WindowResizeEvent& e)
															{ return OnWindowResize(e); });
	dispatcher.Dispatch<ApplicationCore::MouseMovedEvent>([this](ApplicationCore::MouseMovedEvent& e)
														  { return OnMouseMoved(e); });
	dispatcher.Dispatch<ApplicationCore::MouseButtonPressedEvent>([this](ApplicationCore::MouseButtonPressedEvent& e)
																  { return OnMouseButtonPressed(e); });
	dispatcher.Dispatch<ApplicationCore::MouseButtonReleasedEvent>([this](ApplicationCore::MouseButtonReleasedEvent& e)
																   { return OnMouseButtonReleased(e); });
}

void BoardLayer::OnUpdate(float deltaTime)
{
	if (m_AnimationMove)
		m_AnimationDone += deltaTime / m_AnimationsLengthS;
	if (m_AnimationDone > 1)
	{
		m_ChessBoard.MakeMove(m_AnimationMove, true);
		if (m_AnimationMove.GetMoveFlags() & NeraChessEngine::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (m_AnimationMove.GetMoveFlags() & NeraChessEngine::IS_CASTLES)
		{
			PlayRandomSoundFromList(m_MoveSounds);
			PlayRandomSoundFromList(m_MoveSounds);
		}
		else
			PlayRandomSoundFromList(m_MoveSounds);
		m_LastMovePlayed = m_AnimationMove;
		m_AnimationSkipped = false;
		m_AnimationDone = 0.f;
		m_AnimationMove = 0;
		m_AnimationPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_AnimationFromSquare = 64;
	}
}

void BoardLayer::OnRender()
{
	DrawBoard(); // done

	DrawHighlights(); // done

	// Draw Square names

	DrawPieces();

	// Draw animated pieces
	DrawAnimatedPiece();

	// Draw flying pieces
	DrawFlyingPiece();

	// Draw Timer / Players info
}

void BoardLayer::DrawBoard()
{
	for (uint8_t file = 0; file < 8; file++)
	{
		for (uint8_t rank = 0; rank < 8; rank++)
		{
			bool isLightSquare = (file + rank) % 2 == 0;
			ApplicationCore::Color squareColor = isLightSquare ? m_LightSquareColor : m_DarkSquareColor;

			ApplicationCore::Vec2<uint32_t> squarePos = {m_Margin.X + file * m_SquareSize.X,
														 m_Margin.Y + rank * m_SquareSize.Y};

			m_Renderer.DrawSquare(squarePos, m_SquareSize, squareColor);
		}
	}
}

void BoardLayer::DrawHighlights()
{
	// Draw Last move if available
	if (m_LastMovePlayed)
	{
		NeraChessEngine::Square fromSquare = m_LastMovePlayed.GetStartSquare();
		NeraChessEngine::Square targetSquare = m_LastMovePlayed.GetTargetSquare();

		uint8_t fromFile = m_WhiteBottom ? fromSquare.GetFile() : 7 - fromSquare.GetFile();
		uint8_t fromRank = m_WhiteBottom ? 7 - fromSquare.GetRank() : fromSquare.GetRank();

		uint8_t targetFile = m_WhiteBottom ? targetSquare.GetFile() : 7 - targetSquare.GetFile();
		uint8_t targetRank = m_WhiteBottom ? 7 - targetSquare.GetRank() : targetSquare.GetRank();

		ApplicationCore::Vec2<uint32_t> squarePos = {m_Margin.X + fromFile * m_SquareSize.X,
													 m_Margin.Y + fromRank * m_SquareSize.Y};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_LastMoveColor);

		squarePos = {m_Margin.X + targetFile * m_SquareSize.X, m_Margin.Y + targetRank * m_SquareSize.Y};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_LastMoveColor);
	}

	// Draw Selected Piece Squares

	if (m_SelectedPieceSquare != 64)
	{
		uint8_t file = m_WhiteBottom ? m_SelectedPieceSquare.GetFile() : 7 - m_SelectedPieceSquare.GetFile();
		uint8_t rank = m_WhiteBottom ? 7 - m_SelectedPieceSquare.GetRank() : m_SelectedPieceSquare.GetRank();

		ApplicationCore::Vec2<uint32_t> squarePos = {m_Margin.X + file * m_SquareSize.X,
													 m_Margin.Y + rank * m_SquareSize.Y};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_SelectedPieceColor);
	}

	// Draw Things the player has higlighted

	NeraChessEngine::Bitboard squares = m_MarkedSquares;

	while (squares)
	{
		NeraChessEngine::Square square = NeraChessEngine::BitUtil::PopLSB(squares);

		uint8_t file = m_WhiteBottom ? square.GetFile() : 7 - square.GetFile();
		uint8_t rank = m_WhiteBottom ? 7 - square.GetRank() : square.GetRank();

		ApplicationCore::Vec2<uint32_t> squarePos = {m_Margin.X + file * m_SquareSize.X,
													 m_Margin.Y + rank * m_SquareSize.Y};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_MarkedSquareColor);
	}
}

void BoardLayer::DrawAnimatedPiece()
{
	if (m_AnimationPiece == NeraChessEngine::PieceType::NO_PIECE)
		return;

	uint8_t fromFile = m_WhiteBottom ? m_AnimationFromSquare.GetFile() : 7 - m_AnimationFromSquare.GetFile();
	uint8_t fromRank = m_WhiteBottom ? 7 - m_AnimationFromSquare.GetRank() : m_AnimationFromSquare.GetRank();

	ApplicationCore::Vec2<uint32_t> fromPos = {m_Margin.X + fromFile * m_SquareSize.X,
											   m_Margin.Y + fromRank * m_SquareSize.Y};

	NeraChessEngine::Square targetSquare = m_AnimationMove.GetTargetSquare();

	uint8_t targetFile = m_WhiteBottom ? targetSquare.GetFile() : 7 - targetSquare.GetFile();
	uint8_t targetRank = m_WhiteBottom ? 7 - targetSquare.GetRank() : targetSquare.GetRank();

	ApplicationCore::Vec2<uint32_t> targetPos = {m_Margin.X + targetFile * m_SquareSize.X,
												 m_Margin.Y + targetRank * m_SquareSize.Y};

	ApplicationCore::Vec2<uint32_t> piecePos{
		uint32_t(fromPos.X + long((long(targetPos.X) - long(fromPos.X)) * m_AnimationDone)),
		uint32_t(fromPos.Y + long((long(targetPos.Y) - long(fromPos.Y)) * m_AnimationDone))};

	m_Renderer.DrawSprite(m_PieceSprites[m_AnimationPiece], piecePos, m_SquareSize);
}

void BoardLayer::DrawPieces()
{
	for (NeraChessEngine::Square square = 0; square < 64; square++)
	{
		NeraChessEngine::Piece piece = m_ChessBoard.GetPiece(square);
		if (piece == NeraChessEngine::PieceType::NO_PIECE)
			continue;

		uint8_t file = square.GetFile();
		uint8_t rank = square.GetRank();

		if (file + 8 * rank == m_FlyingPieceSquare || file + 8 * rank == m_AnimationFromSquare)
			continue;

		file = m_WhiteBottom ? file : 7 - file;
		rank = m_WhiteBottom ? rank : 7 - rank;

		ApplicationCore::Vec2<uint32_t> squarePos = {m_Margin.X + file * m_SquareSize.X,
													 m_Margin.Y + (7 - rank) * m_SquareSize.Y};

		m_Renderer.DrawSprite(m_PieceSprites[piece], squarePos, m_SquareSize);
	}
}

void BoardLayer::DrawFlyingPiece()
{
	if (m_FlyingPiece == NeraChessEngine::PieceType::NO_PIECE)
		return;

	m_Renderer.DrawSprite(m_PieceSprites[m_FlyingPiece],
						  m_MousePosition - ApplicationCore::Vec2<uint32_t>(uint32_t(m_SquareSize.X * 0.5),
																			uint32_t(m_SquareSize.Y * 0.5)),
						  m_SquareSize);
}

void BoardLayer::TryMakeMove(NeraChessEngine::Move move)
{
	if (m_AcceptingHumanMove.exchange(false))
		m_HumanMoveQueue.Push(move);
}

void BoardLayer::AddSoundsToList(const std::filesystem::path& path, std::vector<ApplicationCore::Sound>& list)
{
	if (!std::filesystem::is_directory(path))
		throw std::runtime_error("Sound directory is missing: " + path.string());

	std::vector<std::filesystem::path> soundPaths;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path))
	{
		if (entry.is_regular_file())
			soundPaths.push_back(entry.path());
	}

	std::ranges::sort(soundPaths);
	for (const std::filesystem::path& soundPath : soundPaths)
		list.emplace_back(soundPath.string());
}

void BoardLayer::PlayRandomSoundFromList(const std::vector<ApplicationCore::Sound>& sounds)
{
	if (sounds.empty())
		return;

	std::uniform_int_distribution<size_t> distribution(0, sounds.size() - 1);
	const size_t soundIndex = distribution(m_RandomGenerator);

	m_SoundPlayer.PlaySound(sounds[soundIndex]);
}

void BoardLayer::PlayMove(NeraChessEngine::Move move)
{
	if (m_AnimationMove)
	{
		m_ChessBoard.MakeMove(m_AnimationMove, true);
		if (m_AnimationMove.GetMoveFlags() & NeraChessEngine::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (m_AnimationMove.GetMoveFlags() & NeraChessEngine::IS_CASTLES)
		{
			PlayRandomSoundFromList(m_MoveSounds);
			PlayRandomSoundFromList(m_MoveSounds);
		}
		else
			PlayRandomSoundFromList(m_MoveSounds);
		m_LastMovePlayed = m_AnimationMove;
		m_AnimationDone = 0.f;
		m_AnimationMove = 0;
		m_AnimationPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_AnimationFromSquare = 64;
	}

	if (m_AnimationSkipped)
	{
		if (move.GetMoveFlags() & NeraChessEngine::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (move.GetMoveFlags() & NeraChessEngine::IS_CASTLES)
		{
			PlayRandomSoundFromList(m_MoveSounds);
			PlayRandomSoundFromList(m_MoveSounds);
		}
		else
			PlayRandomSoundFromList(m_MoveSounds);
		m_AnimationSkipped = false;
		m_ChessBoard.MakeMove(move, true);
		m_LastMovePlayed = move;
		return;
	}
	else
	{
		m_AnimationDone = 0.f;
		m_AnimationMove = move;
		m_AnimationFromSquare = move.GetStartSquare();
		m_AnimationPiece = move.GetMovePiece();
	}
}

void BoardLayer::BeginHumanMoveInput()
{
	m_HumanMoveQueue.Clear();
	m_AcceptingHumanMove = true;
}

bool BoardLayer::TryGetHumanMove(NeraChessEngine::Move& move)
{
	return m_HumanMoveQueue.TryPop(move);
}

void BoardLayer::CancelHumanMoveInput()
{
	m_AcceptingHumanMove = false;
	m_HumanMoveQueue.Clear();
}

bool BoardLayer::OnMouseButtonPressed(ApplicationCore::MouseButtonPressedEvent&)
{
	bool mouseInBoardX = m_Margin.X <= m_MousePosition.X && m_MousePosition.X < m_Margin.X + 8 * m_SquareSize.X;
	bool mouseInBoardY = m_Margin.Y <= m_MousePosition.Y && m_MousePosition.Y < m_Margin.Y + 8 * m_SquareSize.Y;
	bool mouseInBoard = mouseInBoardX && mouseInBoardY;

	if (!mouseInBoard)
		return false;

	uint8_t rank = m_WhiteBottom ? 7 - ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y)
								 : ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y);
	uint8_t file = m_WhiteBottom ? (m_MousePosition.X - m_Margin.X) / m_SquareSize.X
								 : 7 - (m_MousePosition.X - m_Margin.X) / m_SquareSize.X;

	NeraChessEngine::Square square = file + 8 * rank;

	// If we already selected a Square,
	if (m_SelectedPiece != NeraChessEngine::PieceType::NO_PIECE)
	{
		NeraChessEngine::Move move = 0;
		for (NeraChessEngine::Move legalMove : m_ChessBoard.GetLegalMoves())
		{
			if (legalMove.GetStartSquare() == m_SelectedPieceSquare && legalMove.GetTargetSquare() == square)
			{
				move = legalMove;
				break;
			}
		}

		if (move)
			TryMakeMove(move);

		m_FlyingPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		m_SelectedPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_SelectedPieceSquare = 64;
	}
	else
	{
		NeraChessEngine::Piece piece = m_ChessBoard.GetPiece(square);
		if (piece == NeraChessEngine::PieceType::NO_PIECE)
			return true;

		m_FlyingPiece = piece;
		m_FlyingPieceSquare = square;

		m_SelectedPiece = piece;
		m_SelectedPieceSquare = square;
	}

	return true;
}

bool BoardLayer::OnMouseButtonReleased(ApplicationCore::MouseButtonReleasedEvent&)
{
	bool mouseInBoardX = m_Margin.X <= m_MousePosition.X && m_MousePosition.X < m_Margin.X + 8 * m_SquareSize.X;
	bool mouseInBoardY = m_Margin.Y <= m_MousePosition.Y && m_MousePosition.Y < m_Margin.Y + 8 * m_SquareSize.Y;
	bool mouseInBoard = mouseInBoardX && mouseInBoardY;

	if (!mouseInBoard)
	{
		m_FlyingPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		return true;
	}

	uint8_t rank = m_WhiteBottom ? 7 - ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y)
								 : ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y);
	uint8_t file = m_WhiteBottom ? (m_MousePosition.X - m_Margin.X) / m_SquareSize.X
								 : 7 - (m_MousePosition.X - m_Margin.X) / m_SquareSize.X;

	NeraChessEngine::Square square = file + 8 * rank;

	if (m_FlyingPiece != NeraChessEngine::PieceType::NO_PIECE)
	{
		if (square == m_SelectedPieceSquare)
		{
			m_FlyingPiece = NeraChessEngine::PieceType::NO_PIECE;
			m_FlyingPieceSquare = 64;

			return true;
		}

		NeraChessEngine::Move move = 0;
		for (NeraChessEngine::Move legalMove : m_ChessBoard.GetLegalMoves())
		{
			if (legalMove.GetStartSquare() == m_SelectedPieceSquare && legalMove.GetTargetSquare() == square)
			{
				move = legalMove;
				break;
			}
		}

		if (move)
		{
			m_AnimationSkipped = true;
			TryMakeMove(move);
		}

		m_FlyingPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		m_SelectedPiece = NeraChessEngine::PieceType::NO_PIECE;
		m_SelectedPieceSquare = 64;
	}

	return true;
}

bool BoardLayer::OnMouseMoved(ApplicationCore::MouseMovedEvent& event)
{

	m_MousePosition = event.GetPosition();

	return true;
}

bool BoardLayer::OnWindowResize(ApplicationCore::WindowResizeEvent& event)
{
	UpdateSize(event.GetNewSize());

	return false;
}

void BoardLayer::UpdateSize(ApplicationCore::Vec2<uint32_t> windowSize)
{
	uint32_t boardAreaWidth = windowSize.X;
	if (windowSize.X >= NeraChessApp::UILayout::MinimumSideBySideWidth)
		boardAreaWidth -= NeraChessApp::UILayout::SidePanelReservedWidth;

	uint32_t smallerSide = std::min(boardAreaWidth, windowSize.Y);

	uint32_t smallerMargin = uint32_t(smallerSide * m_MarginProportion);

	uint32_t squareSide = std::max(1U, (smallerSide - 2 * smallerMargin) / 8);

	m_SquareSize = {squareSide, squareSide};

	bool isHorizontalWindow = boardAreaWidth > windowSize.Y;

	if (isHorizontalWindow)
	{
		m_Margin.Y = smallerMargin;
		m_Margin.X = (boardAreaWidth - m_SquareSize.X * 8) / 2;
	}
	else
	{
		m_Margin.X = smallerMargin;
		m_Margin.Y = (windowSize.Y - m_SquareSize.Y * 8) / 2;
	}
}
