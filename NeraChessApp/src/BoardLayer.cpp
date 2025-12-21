#include "BoardLayer.h"

#include "Core/Application.h"
#include "Core/Event.h"


#include <string>
#include <vector>
#include <print>
#include <random>
#include <assert.h>

BoardLayer::BoardLayer()
	: m_Renderer(NeraCore::Application::Get().GetWindow()->GetRenderer()),
	m_SoundPlayer(NeraCore::Application::Get().GetWindow()->GetSoundPlayer()),
	m_Texture("Ressources/Sprites/ChessPieces.png"),
	m_PieceSprites(
	{
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture },
		NeraCore::Sprite{ m_Texture } 
	})
{
	UpdateSize(NeraCore::Application::Get().GetWindow()->GetSize());
	
	uint32_t pieceWidth = m_Texture.GetSize().X / 6;
	uint32_t pieceHeight = m_Texture.GetSize().Y / 2;

	for (ChessCore::Piece piece = 0; piece < 12; piece++)
	{
		m_PieceSprites[piece].Position.Y = piece < 6 ? 0 : pieceHeight;
		m_PieceSprites[piece].Size = { pieceWidth,  pieceHeight };
	}

	m_PieceSprites[ChessCore::WHITE_KING].Position.X = pieceWidth * 0;
	m_PieceSprites[ChessCore::WHITE_QUEEN].Position.X = pieceWidth * 1;
	m_PieceSprites[ChessCore::WHITE_BISHOP].Position.X = pieceWidth * 2;
	m_PieceSprites[ChessCore::WHITE_KNIGHT].Position.X = pieceWidth * 3;
	m_PieceSprites[ChessCore::WHITE_ROOK].Position.X = pieceWidth * 4;
	m_PieceSprites[ChessCore::WHITE_PAWN].Position.X = pieceWidth * 5;
	m_PieceSprites[ChessCore::BLACK_KING].Position.X = pieceWidth * 0;
	m_PieceSprites[ChessCore::BLACK_QUEEN].Position.X = pieceWidth * 1;
	m_PieceSprites[ChessCore::BLACK_BISHOP].Position.X = pieceWidth * 2;
	m_PieceSprites[ChessCore::BLACK_KNIGHT].Position.X = pieceWidth * 3;
	m_PieceSprites[ChessCore::BLACK_ROOK].Position.X = pieceWidth * 4;
	m_PieceSprites[ChessCore::BLACK_PAWN].Position.X = pieceWidth * 5;

	AddSoundsToList("Ressources/Sounds/Moves", m_MoveSounds);
	AddSoundsToList("Ressources/Sounds/Captures", m_CaptureSounds);
}

BoardLayer::~BoardLayer()
{
	
}

void BoardLayer::OnEvent(NeraCore::Event& event)
{
	NeraCore::EventDispatcher dispatcher(event);

	dispatcher.Dispatch<NeraCore::WindowResizeEvent>([this](NeraCore::WindowResizeEvent& e) { return OnWindowResize(e); });
	dispatcher.Dispatch<NeraCore::MouseMovedEvent>([this](NeraCore::MouseMovedEvent& e) {return OnMouseMoved(e); });
	dispatcher.Dispatch<NeraCore::MouseButtonPressedEvent>([this](NeraCore::MouseButtonPressedEvent& e) {return OnMouseButtonPressed(e); });
	dispatcher.Dispatch<NeraCore::MouseButtonReleasedEvent>([this](NeraCore::MouseButtonReleasedEvent& e) {return OnMouseButtonReleased(e); });

}

void BoardLayer::OnUpdate(float deltaTime)
{
	if (m_AnimationMove)
		m_AnimationDone += deltaTime / m_AnimationsLengthS;
	if (m_AnimationDone > 1)
	{
		m_ChessBoard.MakeMove(m_AnimationMove, true);
		if (ChessCore::MoveUtil::GetMoveFlags(m_AnimationMove) & ChessCore::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (ChessCore::MoveUtil::GetMoveFlags(m_AnimationMove) & ChessCore::IS_CASTLES)
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
		m_AnimationPiece = ChessCore::PieceType::NO_PIECE;
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
			NeraCore::Color squareColor = isLightSquare ? m_LightSquareColor : m_DarkSquareColor;
			
			NeraCore::Vec2<uint32_t> squarePos = {
				m_Margin.X + file * m_SquareSize.X,
				m_Margin.Y + rank * m_SquareSize.Y
			};

			m_Renderer.DrawSquare(squarePos, m_SquareSize, squareColor);
		}
	}
}

void BoardLayer::DrawHighlights()
{
	// Draw Last move if available
	if (m_LastMovePlayed)
	{
		// TODO: make this prettier and more efficient / more readable / more custom
		ChessCore::Square fromSquare = ChessCore::MoveUtil::GetFromSquare(m_LastMovePlayed);
		ChessCore::Square targetSquare = ChessCore::MoveUtil::GetTargetSquare(m_LastMovePlayed);

		uint8_t fromFile = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(fromSquare) : 
			7 - ChessCore::SquareUtil::GetFile(fromSquare);
		uint8_t fromRank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(fromSquare) : 
			ChessCore::SquareUtil::GetRank(fromSquare);

		uint8_t targetFile = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(targetSquare) : 
			7 - ChessCore::SquareUtil::GetFile(targetSquare);
		uint8_t targetRank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(targetSquare) : 
			ChessCore::SquareUtil::GetRank(targetSquare);

		NeraCore::Vec2<uint32_t> squarePos = {
			m_Margin.X + fromFile * m_SquareSize.X,
			m_Margin.Y + fromRank * m_SquareSize.Y
		};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_LastMoveColor);

		squarePos = {
			m_Margin.X + targetFile * m_SquareSize.X,
			m_Margin.Y + targetRank * m_SquareSize.Y
		};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_LastMoveColor);
	}

	// Draw Selected Piece Squares
	
	if (m_SelectedPieceSquare != 64)
	{
		uint8_t file = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(m_SelectedPieceSquare) : 
			7 - ChessCore::SquareUtil::GetFile(m_SelectedPieceSquare);
		uint8_t rank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(m_SelectedPieceSquare) : 
			ChessCore::SquareUtil::GetRank(m_SelectedPieceSquare);

		NeraCore::Vec2<uint32_t> squarePos = {
			m_Margin.X + file * m_SquareSize.X,
			m_Margin.Y + rank * m_SquareSize.Y
		};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_SelectedPieceColor);
	}

	// Draw Things the player has higlighted

	ChessCore::Bitboard squares = m_MarkedSquares;

	while (squares)
	{
		ChessCore::Square square = ChessCore::BitUtil::PopLSB(squares);

		uint8_t file = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(square) : 
			7 - ChessCore::SquareUtil::GetFile(square);
		uint8_t rank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(square) :
			ChessCore::SquareUtil::GetRank(square);

		NeraCore::Vec2<uint32_t> squarePos = {
			m_Margin.X + file * m_SquareSize.X,
			m_Margin.Y + rank * m_SquareSize.Y
		};

		m_Renderer.DrawSquare(squarePos, m_SquareSize, m_MarkedSquareColor);
	}
}

void BoardLayer::DrawAnimatedPiece()
{
	if (m_AnimationPiece == ChessCore::PieceType::NO_PIECE)
		return;

	uint8_t fromFile = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(m_AnimationFromSquare) :
		7 - ChessCore::SquareUtil::GetFile(m_AnimationFromSquare);
	uint8_t fromRank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(m_AnimationFromSquare) :
		ChessCore::SquareUtil::GetRank(m_AnimationFromSquare);

	NeraCore::Vec2<uint32_t> fromPos = {
		m_Margin.X + fromFile * m_SquareSize.X,
		m_Margin.Y + fromRank * m_SquareSize.Y
	};

	ChessCore::Square targetSquare = ChessCore::MoveUtil::GetTargetSquare(m_AnimationMove);

	uint8_t targetFile = m_WhiteBottom ? ChessCore::SquareUtil::GetFile(targetSquare) : 
		7 - ChessCore::SquareUtil::GetFile(targetSquare);
	uint8_t targetRank = m_WhiteBottom ? 7 - ChessCore::SquareUtil::GetRank(targetSquare) : 
		ChessCore::SquareUtil::GetRank(targetSquare);

	NeraCore::Vec2<uint32_t> targetPos = {
		m_Margin.X + targetFile * m_SquareSize.X,
		m_Margin.Y + targetRank * m_SquareSize.Y
	};

	NeraCore::Vec2<uint32_t> piecePos
	{ 
		uint32_t(fromPos.X + long((long(targetPos.X) - long(fromPos.X)) * m_AnimationDone)),
		uint32_t(fromPos.Y + long((long(targetPos.Y) - long(fromPos.Y)) * m_AnimationDone))
	};

	m_Renderer.DrawSprite(
		m_PieceSprites[m_AnimationPiece],
		piecePos,
		m_SquareSize
	);

}

void BoardLayer::DrawPieces()
{
	for (ChessCore::Square square = 0; square < 64; square++)
	{
		ChessCore::Piece piece = m_ChessBoard.GetPiece(square);
		if (piece == ChessCore::PieceType::NO_PIECE)
			continue;

		uint8_t file = ChessCore::SquareUtil::GetFile(square);
		uint8_t rank = ChessCore::SquareUtil::GetRank(square);
		
		if (file + 8 * rank == m_FlyingPieceSquare ||
			file + 8 * rank == m_AnimationFromSquare)
			continue;

		file = m_WhiteBottom ? file : 7 - file;
		rank = m_WhiteBottom ? rank : 7 - rank;

		NeraCore::Vec2<uint32_t> squarePos = {
			m_Margin.X + file * m_SquareSize.X,
			m_Margin.Y + (7 - rank) * m_SquareSize.Y
		};

		m_Renderer.DrawSprite(
			m_PieceSprites[piece],
			squarePos,
			m_SquareSize
		);
	}

}

void BoardLayer::DrawFlyingPiece()
{
	if (m_FlyingPiece == ChessCore::PieceType::NO_PIECE)
		return;

	m_Renderer.DrawSprite(
		m_PieceSprites[m_FlyingPiece],
		m_MousePosition - NeraCore::Vec2<uint32_t>(uint32_t(m_SquareSize.X * 0.5), uint32_t(m_SquareSize.Y * 0.5)),
		m_SquareSize
	);
}

void BoardLayer::TryMakeMove(ChessCore::Move move)
{
	if (m_MovePtr)
	{
		*m_MovePtr = move;
	}
	m_MovePtr = nullptr;
}

void BoardLayer::AddSoundsToList(std::filesystem::path path, std::vector<NeraCore::Sound>& list)
{

	if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path))
		{
			if (entry.is_regular_file())
			{
				list.emplace_back(path.string() + "/" + entry.path().filename().string());
			}
		}
	}
	else
	{
		std::println("Error: Folder doesn't exist: {}", path.string());
		//assert(false);
	}

}


void BoardLayer::PlayRandomSoundFromList(const std::vector<NeraCore::Sound>& sounds)
{
	if (sounds.empty())
	{
		// TODO: add little error message
		return;
	}

	std::random_device randomDevice;
	std::mt19937 generator(randomDevice());

	std::uniform_int_distribution<size_t> distribution(0, sounds.size() - 1);

	size_t soundIndex = distribution(generator);

	m_SoundPlayer.PlaySound(sounds[soundIndex]);
}

void BoardLayer::PlayMove(ChessCore::Move move)
{
	if (m_AnimationMove)
	{
		m_ChessBoard.MakeMove(m_AnimationMove, true);
		if (ChessCore::MoveUtil::GetMoveFlags(m_AnimationMove) & ChessCore::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (ChessCore::MoveUtil::GetMoveFlags(m_AnimationMove) & ChessCore::IS_CASTLES)
		{
			PlayRandomSoundFromList(m_MoveSounds);
			PlayRandomSoundFromList(m_MoveSounds);
		}
		else
			PlayRandomSoundFromList(m_MoveSounds);
		m_LastMovePlayed = m_AnimationMove;
		m_AnimationDone = 0.f;
		m_AnimationMove = 0;
		m_AnimationPiece = ChessCore::PieceType::NO_PIECE;
		m_AnimationFromSquare = 64;
	}

	if (m_AnimationSkipped)
	{
		if (ChessCore::MoveUtil::GetMoveFlags(move) & ChessCore::IS_CAPTURE)
			PlayRandomSoundFromList(m_CaptureSounds);
		else if (ChessCore::MoveUtil::GetMoveFlags(move) & ChessCore::IS_CASTLES)
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
		m_AnimationFromSquare = ChessCore::MoveUtil::GetFromSquare(move);
		m_AnimationPiece = ChessCore::MoveUtil::GetMovePiece(move);
	}

}

void BoardLayer::SetMovePtr(ChessCore::Move* move)
{
	m_MovePtr = move;
}

bool BoardLayer::OnMouseButtonPressed(NeraCore::MouseButtonPressedEvent& event)
{
	bool mouseInBoardX = m_Margin.X <= m_MousePosition.X && m_MousePosition.X <= m_Margin.X + 8 * m_SquareSize.X;
	bool mouseInBoardY = m_Margin.Y <= m_MousePosition.Y && m_MousePosition.Y <= m_Margin.Y + 8 * m_SquareSize.Y;
	bool mouseInBoard = mouseInBoardX && mouseInBoardY;

	if (!mouseInBoard)
		return false;

	uint8_t rank = m_WhiteBottom ? 7 - ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y) :
		((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y);
	uint8_t file = m_WhiteBottom ? (m_MousePosition.X - m_Margin.X) / m_SquareSize.X : 
		7 - (m_MousePosition.X - m_Margin.X) / m_SquareSize.X;

	ChessCore::Square square = file + 8 * rank;

	// If we already selected a Square,
	if (m_SelectedPiece != ChessCore::PieceType::NO_PIECE)
	{
		ChessCore::Move move = 0;
		// TODO: fix this, don't recalculate this here everytime
		m_LegalMoves = m_ChessBoard.GetLegalMoves();
		for (ChessCore::Move legalMove : m_LegalMoves)
		{
			if (ChessCore::MoveUtil::GetFromSquare(legalMove) == m_SelectedPieceSquare && 
				ChessCore::MoveUtil::GetTargetSquare(legalMove) == square)
			{
				move = legalMove;
				break;
			}
		}

		if (move)
			TryMakeMove(move);

		m_FlyingPiece = ChessCore::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		m_SelectedPiece = ChessCore::PieceType::NO_PIECE;
		m_SelectedPieceSquare = 64;
	}
	else
	{
		ChessCore::Piece piece = m_ChessBoard.GetPiece(square);
		if (piece == ChessCore::PieceType::NO_PIECE)
			return true;

		m_FlyingPiece = piece;
		m_FlyingPieceSquare = square;

		m_SelectedPiece = piece;
		m_SelectedPieceSquare = square;

	}

	return true;
}

bool BoardLayer::OnMouseButtonReleased(NeraCore::MouseButtonReleasedEvent& event)
{
	bool mouseInBoardX = m_Margin.X <= m_MousePosition.X && m_MousePosition.X <= m_Margin.X + 8 * m_SquareSize.X;
	bool mouseInBoardY = m_Margin.Y <= m_MousePosition.Y && m_MousePosition.Y <= m_Margin.Y + 8 * m_SquareSize.Y;
	bool mouseInBoard = mouseInBoardX && mouseInBoardY;

	if (!mouseInBoard)
	{
		m_FlyingPiece = ChessCore::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		return true;
	}
	
	uint8_t rank = m_WhiteBottom ? 7 - ((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y) :
		((m_MousePosition.Y - m_Margin.Y) / m_SquareSize.Y);
	uint8_t file = m_WhiteBottom ? (m_MousePosition.X - m_Margin.X) / m_SquareSize.X : 
		7 - (m_MousePosition.X - m_Margin.X) / m_SquareSize.X;

	ChessCore::Square square = file + 8 * rank;

	if (m_FlyingPiece != ChessCore::PieceType::NO_PIECE)
	{
		if (square == m_SelectedPieceSquare)
		{
			m_FlyingPiece = ChessCore::PieceType::NO_PIECE;
			m_FlyingPieceSquare = 64;

			return true;
		}

		ChessCore::Move move = 0;
		// TODO: fix this, don't recalculate this here everytime
		m_LegalMoves = m_ChessBoard.GetLegalMoves();
		for (ChessCore::Move legalMove : m_LegalMoves)
		{
			if (ChessCore::MoveUtil::GetFromSquare(legalMove) == m_SelectedPieceSquare &&
				ChessCore::MoveUtil::GetTargetSquare(legalMove) == square)
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

		m_FlyingPiece = ChessCore::PieceType::NO_PIECE;
		m_FlyingPieceSquare = 64;

		m_SelectedPiece = ChessCore::PieceType::NO_PIECE;
		m_SelectedPieceSquare = 64;

	}

	return true;
}

bool BoardLayer::OnMouseMoved(NeraCore::MouseMovedEvent& event)
{

	m_MousePosition = event.GetPosition();

	return true;
}

bool BoardLayer::OnWindowResize(NeraCore::WindowResizeEvent& event)
{
	UpdateSize(event.GetNewSize());

	return false;
}

void BoardLayer::UpdateSize(NeraCore::Vec2<uint32_t> windowSize)
{
	uint32_t smallerSide = std::min(windowSize.X, windowSize.Y);

	uint32_t smallerMargin = uint32_t(smallerSide * m_MarginProportion);

	uint32_t squareSide = (smallerSide - 2 * smallerMargin) / 8;

	m_SquareSize = { squareSide, squareSide };

	bool isHorizontalWindow = windowSize.X > windowSize.Y;

	if (isHorizontalWindow)
	{
		m_Margin.Y = smallerMargin;
		m_Margin.X = (windowSize.X - m_SquareSize.X * 8) / 2;
	}
	else
	{
		m_Margin.X = smallerMargin;
		m_Margin.Y = (windowSize.Y - m_SquareSize.Y * 8) / 2;
	}


}
