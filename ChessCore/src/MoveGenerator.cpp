#include "MoveGenerator.h"

#include <memory>
#include <algorithm>

#include "ChessUtil.h"

namespace ChessCore
{

	Bitboard MoveGenerator::GetStraightSlidingMask(Square square)
	{
		Bitboard mask = 0ULL;

		int rank = square / 8;
		int file = square % 8;

		for (int r = rank + 1; r < 7; r++)
			mask |= (1ULL << (r * 8 + file)); // N
		for (int f = file + 1; f < 7; f++)
			mask |= (1ULL << (rank * 8 + f)); // E
		for (int r = rank - 1; r > 0; r--)
			mask |= (1ULL << (r * 8 + file)); // S
		for (int f = file - 1; f > 0; f--)
			mask |= (1ULL << (rank * 8 + f)); // W

		return mask;
	}
	Bitboard MoveGenerator::GetDiagonalSlidingMask(Square square)
	{
		Bitboard mask = 0ULL;

		int rank = square / 8;
		int file = square % 8;

		for (int r = rank + 1, f = file + 1; r < 7 && f < 7; r++, f++)
			mask |= (1ULL << (r * 8 + f)); // NE
		for (int r = rank + 1, f = file - 1; r < 7 && f > 0; r++, f--)
			mask |= (1ULL << (r * 8 + f)); // NW
		for (int r = rank - 1, f = file + 1; r > 0 && f < 7; r--, f++)
			mask |= (1ULL << (r * 8 + f)); // SE
		for (int r = rank - 1, f = file - 1; r > 0 && f > 0; r--, f--)
			mask |= (1ULL << (r * 8 + f)); // SW

		return mask;
	}

	std::vector<Bitboard> MoveGenerator::CreateAllBlockerBitboards(Bitboard mask)
	{
		std::vector<uint8_t> moveSquareIndices{};
		moveSquareIndices.reserve(15);
		for (Square square = 0; square < 64; square++)
		{
			if ((mask >> square) & 1U)
				moveSquareIndices.push_back(square);
		}

		int numPatterns = 1 << moveSquareIndices.size();
		std::vector<Bitboard> blockerBitboards;
		blockerBitboards.reserve(numPatterns);

		for (int patternIndex = 0; patternIndex < numPatterns; patternIndex++)
		{
			Bitboard newBlocker = 0ULL;

			for (int bitIndex = 0; bitIndex < moveSquareIndices.size(); bitIndex++)
			{
				Bitboard bit = (patternIndex >> bitIndex) & 1U;

				if (bit)
					newBlocker |= bit << moveSquareIndices[bitIndex];
			}

			blockerBitboards.push_back(newBlocker);
		}

		return blockerBitboards;
	}

	Bitboard MoveGenerator::CalculatePossibleRookMoves(Square from_square, Bitboard blockers)
	{
		Bitboard moves = 0ULL;

		int rank = from_square / 8;
		int file = from_square % 8;

		for (uint8_t dir = 0; dir < 4; dir++)
		{
			for (uint8_t dst = 1; dst < 8; dst++)
			{
				int r =
					dir == 0 ? rank + dst :
					dir == 1 ? rank :
					dir == 2 ? rank - dst :
					rank;

				int f =
					dir == 0 ? file :
					dir == 1 ? file + dst :
					dir == 2 ? file :
					file - dst;

				if (0 <= r && r < 8 && 0 <= f && f < 8)
				{
					moves |= 1ULL << (r * 8 + f);
					if (blockers & (1ULL << (r * 8 + f)))
						break;
				}
				else
					break;

			}
		}
		return moves;
	}
	Bitboard MoveGenerator::CalculatePossibleBishopMoves(Square from_square, Bitboard blockers)
	{
		Bitboard moves = 0ULL;

		int rank = from_square / 8;
		int file = from_square % 8;

		for (uint8_t dir = 0; dir < 4; dir++)
		{
			for (uint8_t dst = 1; dst < 8; dst++)
			{
				int r =
					dir == 0 ? rank + dst :
					dir == 1 ? rank - dst :
					dir == 2 ? rank - dst :
					rank + dst;

				int f =
					dir == 0 ? file + dst :
					dir == 1 ? file + dst :
					dir == 2 ? file - dst :
					file - dst;

				if (0 <= r && r < 8 && 0 <= f && f < 8)
				{
					moves |= 1ULL << (r * 8 + f);
					if (blockers & (1ULL << (r * 8 + f)))
						break;
				}
				else
					break;

			}
		}
		return moves;
	}

	std::array<Bitboard, 64> MoveGenerator::InitRookMasks()
	{
		std::array<Bitboard, 64> arr{};
		for (uint8_t square = 0; square < 64; square++)
		{
			arr[square] = GetStraightSlidingMask(square);
		}
		return arr;
	}
	std::array<Bitboard, 64> MoveGenerator::InitBishopMasks()
	{
		std::array<Bitboard, 64> arr{};
		for (uint8_t square = 0; square < 64; square++)
		{
			arr[square] = GetDiagonalSlidingMask(square);
		}
		return arr;
	}

	std::unique_ptr<Bitboard[]> MoveGenerator::InitRookMoveMasks()
	{
		std::unique_ptr<Bitboard[]> array = std::make_unique<Bitboard[]>(MoveGenerator::s_RookMoveMaskSize);

		for (uint8_t square = 0; square < 64; square++)
		{
			std::vector<Bitboard> possibleRookBlockers = CreateAllBlockerBitboards(GetStraightSlidingMask(square));
			for (Bitboard currentRookBlocker : possibleRookBlockers)
			{
				unsigned long long index = (uint64_t)square * s_MaxPossibleRookMasks + ((currentRookBlocker * s_RookMagics[square]) >> s_RookShifts[square]);
				array[index] = CalculatePossibleRookMoves(square, currentRookBlocker);
			}
		}

		return array;
	}

	std::unique_ptr<Bitboard[]> MoveGenerator::InitBishopMoveMasks()
	{
		std::unique_ptr<Bitboard[]> array = std::make_unique<Bitboard[]>(MoveGenerator::s_RookMoveMaskSize);

		for (uint8_t square = 0; square < 64; square++)
		{
			std::vector<Bitboard> possibleBishopBlockers = CreateAllBlockerBitboards(GetDiagonalSlidingMask(square));
			for (Bitboard currentBishopBlocker : possibleBishopBlockers)
			{
				unsigned long long index = (uint64_t)square * s_MaxPossibleBishopMasks + ((currentBishopBlocker * s_BishopMagics[square]) >> s_BishopShifts[square]);
				array[index] = CalculatePossibleBishopMoves(square, currentBishopBlocker);
			}
		}

		return array;
	}

	std::array<Bitboard, 64> MoveGenerator::InitKingMoveMask()
	{
		std::array<Bitboard, 64> arr{};

		for (Square square = 0; square < 64; square++)
		{
			const int file = square.GetFile();
			const int rank = square.GetRank();

			for (std::array<int, 2> dir : s_DirectionOffsets2D)
			{			
				int f = file + dir[0];
				int r = rank + dir[1];

				if (Square::IsValidCoordinates(f, r))
				{
					arr[square] |= 1ULL << Square(f, r);
				}
			}

		}
		return arr;
	}

	std::array<Bitboard, 64> MoveGenerator::InitKnightMoveMask()
	{
		std::array<Bitboard, 64> array{};

		for (Square square = 0; square < 64; square++)
		{
			const int file = square.GetFile();
			const int rank = square.GetRank();

			std::array<std::array<int, 2>, 8> knightJumps =
			{ {
				{-2, -1}, 
				{-2,  1}, 
				{-1,  2}, 
				{ 1,  2}, 
				{ 2,  1}, 
				{ 2, -1}, 
				{ 1, -2}, 
				{-1, -2}
			} };

			for (int i = 0; i < knightJumps.size(); i++)
			{
				int knightX = file + knightJumps[i][0];
				int knightY = rank + knightJumps[i][1];
				if (Square::IsValidCoordinates(knightX, knightY))
				{
					array[square] |= 1ULL << Square(knightX, knightY);
				}
			}
		}

		return array;
	}

	std::array<Bitboard, 64> MoveGenerator::InitWhitePawnAttackMasks()
	{
		std::array<Bitboard, 64> array{};


		for (Square square = 0; square < 64; square++)
		{
			const uint8_t file = square.GetFile();
			const uint8_t rank = square.GetRank();

			if (Square::IsValidCoordinates(file + 1, rank + 1))
			{
				array[square] |= 1ULL << Square(file + 1, rank + 1);
			}
			if (Square::IsValidCoordinates(file - 1, rank + 1))
			{
				array[square] |= 1ULL << Square(file - 1, rank + 1);
			}

		}

		return array;
	}

	std::array<Bitboard, 64> MoveGenerator::InitBlackPawnAttackMasks()
	{
		std::array<Bitboard, 64> array{};


		for (Square square = 0; square < 64; square++)
		{
			const uint8_t file = square.GetFile();
			const uint8_t rank = square.GetRank();
		
			if (Square::IsValidCoordinates(file + 1, rank - 1))
			{
				array[square] |= 1ULL << Square(file + 1, rank - 1);
			}
			if (Square::IsValidCoordinates(file - 1, rank - 1))
			{
				array[square] |= 1ULL << Square(file - 1, rank - 1);
			}

		}


		return array;
	}

	std::array<std::array<Bitboard, 64>, 8> MoveGenerator::InitDirRayMask()
	{
		std::array<std::array<Bitboard, 64>, 8> array{};

		for (uint8_t dirIndex = 0; dirIndex < s_DirectionOffsets2D.size(); dirIndex++)
		{
			for (Square square = 0; square < 64; square++)
			{
				const uint8_t file = square.GetFile();
				const uint8_t rank = square.GetRank();

				for (int dst = 0; dst < 8; dst++)
				{
					int f = file + s_DirectionOffsets2D[dirIndex][0] * dst;
					int r = rank + s_DirectionOffsets2D[dirIndex][1] * dst;

					if (Square::IsValidCoordinates(f, r))
					{
						array[dirIndex][square] |= 1ULL << Square(f, r);
					}
					else
					{
						break;
					}
				}
			}
		}

		return array;
	}

	std::array<std::array<int, 8>, 64> MoveGenerator::InitSquaresToEdge()
	{
		std::array<std::array<int, 8>, 64> array{};

		for (int squareIndex = 0; squareIndex < 64; squareIndex++)
		{

			int y = squareIndex / 8;
			int x = squareIndex - y * 8;

			int north = 7 - y;
			int south = y;
			int west = x;
			int east = 7 - x;
	
			array[squareIndex][0] = north;
			array[squareIndex][1] = east;
			array[squareIndex][2] = south;
			array[squareIndex][3] = west;
			array[squareIndex][4] = std::min(north, east);
			array[squareIndex][5] = std::min(south, east);
			array[squareIndex][6] = std::min(south, west);
			array[squareIndex][7] = std::min(north, west);
		}

		return array;
	}

	std::array<std::array<Bitboard, 64>, 64> MoveGenerator::InitAlignMask()
	{
		static std::array<std::array<Bitboard, 64>, 64> array{};
	
		for (Square squareA = 0; squareA < 64; squareA++)
		{
			for (Square squareB = 0; squareB < 64; squareB++)
			{

				uint8_t pointAFile = squareA.GetFile();
				uint8_t pointARank = squareA.GetRank();

				uint8_t pointBFile = squareB.GetFile();
				uint8_t pointBRank = squareB.GetRank();

				int deltaFile = pointBFile - pointAFile;
				int deltaRank = pointBRank - pointARank;

				int dirFile = 
					deltaFile > 0 ?  1 : 
					deltaFile < 0 ? -1 : 
					0;

				int dirRank =
					deltaRank > 0 ?  1 :
					deltaRank < 0 ? -1 : 
					0;


				for (int i = -8; i < 8; i++)
				{
					int coordFile = pointAFile + dirFile * i;
					int coordRank = pointARank + dirRank * i;

					if (Square::IsValidCoordinates(coordFile, coordRank))
					{
						array[squareA][squareB] |= 1ULL << Square(coordFile, coordRank);
					}
				}
			}
		}

	
		return array;
	}

	const std::array<int, 8> MoveGenerator::s_DirectionOffsets = { 8, 1, -8, -1, 9, -7, -9, 7 };
	const std::array<std::array<int, 2>, 8>  MoveGenerator::s_DirectionOffsets2D =
	{ {
		{ 0, 1 }, // NORTH
		{ 1, 0 }, // EAST
		{ 0,-1 }, // SOUTH
		{-1, 0 }, // WEST
		{ 1, 1 }, // NORTH-EAST
		{ 1,-1 }, // SOUTH-EAST
		{-1,-1 }, // SOUTH-WEST
		{-1, 1 }  // NORTH-WEST
	} };

	const std::array<uint8_t, 64> MoveGenerator::s_RookShifts = { 52, 53, 53, 53, 53, 53, 53, 52, 53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53, 52, 53, 53, 52, 52, 53, 53, 52 };
	const std::array<uint8_t, 64> MoveGenerator::s_BishopShifts = { 58, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 57, 57, 57, 57, 59, 59, 59, 59, 57, 55, 55, 57, 59, 59, 59, 59, 57, 55, 55, 57, 59, 59, 59, 59, 57, 57, 57, 57, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 58 };

	const std::array<uint64_t, 64> MoveGenerator::s_RookMagics = { 9547631778034934032, 2540031495509114944, 144126734022869024, 72066527644352768, 684573532780249728, 3602897590569673216, 72078484826206720, 72057733705170946, 3459468203411914752, 6896411809415808, 4902449739273863425, 405886987824798210, 9223513066409334784, 72198348706283904, 38843583326863620, 38843583326863620, 9223654611888374080, 9642992121978880, 1157425241740804256, 145110246652645376, 2534374570854400, 576602039648257024, 36033195100078344, 76572188787146884, 600893842079744, 5206477829663883338, 40549990981959808, 36037608145420416, 72629344379666768, 2305988146896175232, 10377718525713713705, 145294989822300484, 578853564760195616, 189221555258008840, 576534432752607232, 145110246652645376, 28297033425093632, 18436619589653504, 38843583326863620, 2954361909639381060, 2323927783189151745, 72444627768098824, 54397240455856144, 1157425138602377232, 36037593179127936, 576480543579865216, 324260272715890816, 3387131808579585, 6917819574402416896, 6896411809415808, 40549990981959808, 36037608145420416, 141459043123328, 72198348706283904, 571754662593536, 4400198257152, 4575068227109409, 37295511727907078, 1196406106886169, 914828308643842, 2306125910396895233, 54324825124506121, 9223976802882457860, 1019009924040228930 };
	const std::array<uint64_t, 64> MoveGenerator::s_BishopMagics = { 148619892600473088, 290484652082266500, 148619892600473088, 2306973309314531344, 1450728695758062096, 4945238401398865952, 437139439488614424, 792740221539328514, 1497672410529856, 9150447170093186, 44057977971264, 612491791328904194, 6341503701806481409, 220676965928140802, 1441155183740469504, 76567799333651456, 6935543632376629637, 9150447170093186, 2251868535308308, 146403289020272640, 9225659038254498050, 4612249010261073921, 38027726347060226, 4661955075971080, 153131252688831501, 294440452730650768, 862017267433506, 144704528522952712, 4611967562140368904, 72567784243720, 2743255397968316450, 8358964582840026112, 2306705301418616064, 77704703491875841, 72128102369591554, 9042417988731140, 576553128460042496, 1153486112418760738, 148619892600473088, 290484652082266500, 1234417875809222656, 81570824520768, 1252073270656765956, 432345703025674242, 36029905154081824, 166668405082965025, 571754662593536, 307375090601820224, 437139439488614424, 2306425202970460544, 218570002072110338, 290484652082266500, 70437497344024, 22572983652515856, 578855505878059096, 290484652082266500, 792740221539328514, 76567799333651456, 4611686022923879428, 108088590088799232, 35185446160642, 2251954567779072, 1497672410529856, 148619892600473088 };

	const std::array<Bitboard, 64> MoveGenerator::s_RookMasks = InitRookMasks();
	const std::array<Bitboard, 64> MoveGenerator::s_BishopMasks = InitBishopMasks();

	const std::unique_ptr<Bitboard[]> MoveGenerator::s_RookMoveMasksArray = InitRookMoveMasks();
	const std::unique_ptr<Bitboard[]> MoveGenerator::s_BishopMoveMasksArray = InitBishopMoveMasks();

	const std::array<Bitboard, 64> MoveGenerator::s_KingMoveMask = InitKingMoveMask();
	const std::array<Bitboard, 64> MoveGenerator::s_KnightMoveMask = InitKnightMoveMask();

	const std::array<Bitboard, 64> MoveGenerator::s_WhitePawnAttackMasks = InitWhitePawnAttackMasks();
	const std::array<Bitboard, 64> MoveGenerator::s_BlackPawnAttackMasks = InitBlackPawnAttackMasks();

	const std::array<std::array<Bitboard, 64>, 8> MoveGenerator::s_DirRayMask = InitDirRayMask();
	const std::array<std::array<int, 8>, 64> MoveGenerator::s_SquaresToEdge = InitSquaresToEdge();

	const Bitboard MoveGenerator::s_WhiteKingsideMask = 1ULL << Square::f1 | 1ULL << Square::g1;
	const Bitboard MoveGenerator::s_BlackKingsideMask = 1ULL << Square::f8 | 1ULL << Square::g8;

	const Bitboard MoveGenerator::s_WhiteQueensideMask2 = 1ULL << Square::d1 | 1ULL << Square::c1;
	const Bitboard MoveGenerator::s_BlackQueensideMask2 = 1ULL << Square::d8 | 1ULL << Square::c8;

	const Bitboard MoveGenerator::s_WhiteQueensideMask = 1ULL << Square::d1 | 1ULL << Square::c1 | 1ULL << Square::b1;
	const Bitboard MoveGenerator::s_BlackQueensideMask = 1ULL << Square::d8 | 1ULL << Square::c8 | 1ULL << Square::b8;

	const std::array<std::array<Bitboard, 64>, 64> MoveGenerator::s_AlignMask = InitAlignMask();

	MoveList<218> MoveGenerator::GenerateMoves(const BoardState& board)
	{
		m_BoardState = board;

		m_LegalMoves.clear();
	
		InitGen();

		CalculateKingMoves();

		// If King is in Double Check, we only need King moves
		if (m_InDoubleCheck)
		{
			return m_LegalMoves;
		}

		CalculateSlidingMoves();
		CalculateKnightMoves();
		CalculatePawnMoves();

		return m_LegalMoves;
	}

	void MoveGenerator::InitGen()
	{
		// Reset Variables?
		m_PinRays = 0ULL;
		m_CheckRayBitmask = 0ULL;

		m_InCheck = false; // check
		m_InDoubleCheck = false; // check

		// Convenience
		m_WhiteToMove = m_BoardState.boardStateFlags & (uint8_t)BoardStateFlags::WhiteToMove;

		m_WhitePieces = 
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN] | 
			m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING];

		m_BlackPieces =
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN] |
			m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING];

		m_AllPieces = m_WhitePieces | m_BlackPieces;

		if (m_WhiteToMove)
		{
			m_FriendlyPieces = m_WhitePieces;
			m_OpponentPieces = m_BlackPieces; 

			m_FriendlyKingSquare = BitUtil::GetLSBIndex(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING]);
			m_OpponentKingSquare = BitUtil::GetLSBIndex(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING]);

			m_OpponentOrthogonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] | m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN];
			m_OpponentDiagonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP] | m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN];

			m_FriendlyOrthogonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] | m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN];
			m_FriendlyDiagonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP] | m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN];

			m_FriendlyPawns = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN];
			m_FriendlyKinghts = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT];
			m_FriendlyKing = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING];

			m_OpponentPawns = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN];
			m_OpponentBishops = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP];
			m_OpponentKnights = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT];
			m_OpponentRooks = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK];
			m_OpponentQueens = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN];
		}
		else
		{
			m_FriendlyPieces = m_BlackPieces;
			m_OpponentPieces = m_WhitePieces;

			m_FriendlyKingSquare = BitUtil::GetLSBIndex(m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING]);
			m_OpponentKingSquare = BitUtil::GetLSBIndex(m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KING]);

			m_OpponentOrthogonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK] | m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN];
			m_OpponentDiagonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP] | m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN];

			m_FriendlyOrthogonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_ROOK] | m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN];
			m_FriendlyDiagonalSliders = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_BISHOP] | m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_QUEEN];

			m_FriendlyPawns = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_PAWN];
			m_FriendlyKinghts = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KNIGHT];
			m_FriendlyKing = m_BoardState.pieceBitboards[(uint8_t)PieceType::BLACK_KING];

			m_OpponentPawns = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_PAWN];
			m_OpponentBishops = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_BISHOP];
			m_OpponentKnights = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_KNIGHT];
			m_OpponentRooks = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_ROOK];
			m_OpponentQueens = m_BoardState.pieceBitboards[(uint8_t)PieceType::WHITE_QUEEN];
		}

		CalculateAttackMaps();

	}

	void MoveGenerator::CalculateAttackMaps()
	{
		GenSlidingAttacks();

		uint8_t startDirIndex = 0;
		uint8_t endDirIndex = 8;

		if (m_OpponentQueens == 0)
		{
			startDirIndex = (m_OpponentRooks) ? 0 : 4;
			endDirIndex = (m_OpponentBishops) ? 8 : 4;
		}

	
		for (uint8_t dir = startDirIndex; dir < endDirIndex; dir++)
		{
			bool isDiagonal = dir > 3;
			Bitboard slider = isDiagonal ? m_OpponentDiagonalSliders : m_OpponentOrthogonalSliders;
			// If there are no sliders in this direction, skip it
			if ((s_DirRayMask[dir][m_FriendlyKingSquare] & slider) == 0)
			{
				continue;
			}

			int n = s_SquaresToEdge[m_FriendlyKingSquare][dir];
			int directionOffset = s_DirectionOffsets[dir];
			bool isFriendlyPieceAlongRay = false;
			Bitboard rayMask = 0ULL;	

			for (int i = 0; i < n; i++)
			{
				int squareIndex = m_FriendlyKingSquare + directionOffset * (i + 1);
				rayMask |= 1ULL << squareIndex;

				
				if (m_AllPieces & (1ULL << squareIndex))
				{
					if (m_FriendlyPieces & (1ULL << squareIndex))
					{
						
						if (!isFriendlyPieceAlongRay)
						{
							isFriendlyPieceAlongRay = true;
						}
						
						else
						{
							break;
						}
					}
					
					else
					{
						Piece piece = GetPiece(squareIndex);

						
						if (isDiagonal && piece.IsDiagonalSlider() || !isDiagonal && piece.IsOrthogonalSlider())
						{
							
							if (isFriendlyPieceAlongRay)
							{
								m_PinRays |= rayMask;
							}
							
							else
							{
								m_CheckRayBitmask |= rayMask;
								m_InDoubleCheck = m_InCheck;
								m_InCheck = true;
							}
							break;
						}
						else
						{
							break;
						}
					}
				}
			}
			if (m_InDoubleCheck)
			{
				break;
			}
		}

		m_NotPinRays = ~m_PinRays;

		m_OpponentKnightAttacks = 0ULL;
		Bitboard knights = m_OpponentKnights;
		Bitboard friendlyKingBoard = m_FriendlyKing;

		while (knights != 0)
		{
			int knightSquare = BitUtil::PopLSB(knights);
			Bitboard knightAttacks = s_KnightMoveMask[knightSquare];
			m_OpponentKnightAttacks |= knightAttacks;

			if ((knightAttacks & friendlyKingBoard) != 0)
			{
				m_InDoubleCheck = m_InCheck;
				m_InCheck = true;
				m_CheckRayBitmask |= 1ULL << knightSquare;
			}
		}

		m_OpponentPawnAttackMap = 0;

		Bitboard opponentPawnsBoard = m_OpponentPawns;
		m_OpponentPawnAttackMap = PawnAttacks(opponentPawnsBoard, !m_WhiteToMove);
		if (m_FriendlyKingSquare.ContainsSquare(m_OpponentPawnAttackMap))
		{
			m_InDoubleCheck = m_InCheck;
			m_InCheck = true;
			Bitboard possiblePawnAttackOrigins = m_WhiteToMove ? s_WhitePawnAttackMasks[m_FriendlyKingSquare] : s_BlackPawnAttackMasks[m_FriendlyKingSquare];
			Bitboard pawnCheckMap = opponentPawnsBoard & possiblePawnAttackOrigins;
			m_CheckRayBitmask |= pawnCheckMap;
		}

		m_OpponentAttackMapNoPawns = m_OpponentSlidingAttackMap | m_OpponentKnightAttacks | s_KingMoveMask[m_OpponentKingSquare];
		m_OpponentAttackMap = m_OpponentAttackMapNoPawns | m_OpponentPawnAttackMap;

		if (!m_InCheck)
		{
			m_CheckRayBitmask = ~(0ULL);
		}
	}

	void MoveGenerator::GenSlidingAttacks()
	{
		m_OpponentSlidingAttackMap = 0ULL;
		const Bitboard blockers = m_AllPieces & ~(1ULL << m_FriendlyKingSquare);

		Bitboard opponentOrthogonalSliders = m_OpponentOrthogonalSliders;

		while (opponentOrthogonalSliders)
		{
			int startSquare = BitUtil::PopLSB(opponentOrthogonalSliders);
			Bitboard moveBoard = GetSlidingAttacks(startSquare, blockers, true);
			m_OpponentSlidingAttackMap |= moveBoard;
		}

		Bitboard opponentDiagonalSliders = m_OpponentDiagonalSliders;

		while (opponentDiagonalSliders)
		{
			int startSquare = BitUtil::PopLSB(opponentDiagonalSliders);
			Bitboard moveBoard = GetSlidingAttacks(startSquare, blockers, false);
			m_OpponentSlidingAttackMap |= moveBoard;
		}
	}

	void MoveGenerator::CalculateKingMoves()
	{
		Bitboard legalMask = ~(m_OpponentAttackMap | m_FriendlyPieces);
		Bitboard kingMoves = s_KingMoveMask[m_FriendlyKingSquare] & legalMask;

		while (kingMoves != 0)
		{
			int targetSquare = BitUtil::PopLSB(kingMoves);
			m_LegalMoves.push(Move(
				m_FriendlyKingSquare, 
				targetSquare, 
				m_WhiteToMove ? PieceType::WHITE_KING : PieceType::BLACK_KING,
				0,
				GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE
			));
		}

		// Castling
		if (m_InCheck)
			return;

		Bitboard castleBlockers = m_OpponentAttackMap | m_AllPieces;
		bool canCastleKing = m_WhiteToMove ? (m_BoardState.boardStateFlags & (uint8_t)BoardStateFlags::CanWhiteCastleKing) : (m_BoardState.boardStateFlags & (uint8_t)BoardStateFlags::CanBlackCastleKing);
		if (canCastleKing)
		{
			Bitboard castleMask = m_WhiteToMove ? s_WhiteKingsideMask : s_BlackKingsideMask;
			if ((castleMask & castleBlockers) == 0)
			{
				int targetSquare = m_WhiteToMove ? Square::g1 : Square::g8;
				m_LegalMoves.push(Move(
					m_FriendlyKingSquare, 
					targetSquare,
					GetPiece(m_FriendlyKingSquare),
					0,
					MoveFlags::IS_CASTLES
				));
			}
		}

		bool canCastleQueen = m_WhiteToMove ? (m_BoardState.boardStateFlags & (uint8_t)BoardStateFlags::CanWhiteCastleQueen) : (m_BoardState.boardStateFlags & (uint8_t)BoardStateFlags::CanBlackCastleQueen);
		if (canCastleQueen)
		{
			Bitboard castleMask = m_WhiteToMove ? s_WhiteQueensideMask2 : s_BlackQueensideMask2;
			Bitboard castleBlockMask = m_WhiteToMove ? s_WhiteQueensideMask : s_BlackQueensideMask;
			if ((castleMask & castleBlockers) == 0 && (castleBlockMask & m_AllPieces) == 0)
			{
				int targetSquare = m_WhiteToMove ? Square::c1 : Square::c8;
				m_LegalMoves.push(Move(
					m_FriendlyKingSquare,
					targetSquare,
					GetPiece(m_FriendlyKingSquare),
					0,
					MoveFlags::IS_CASTLES
				));
			}
		}

	}

	void MoveGenerator::CalculateSlidingMoves()
	{
		Bitboard moveMask = ~m_FriendlyPieces & m_CheckRayBitmask;

		Bitboard othogonalSliders = m_FriendlyOrthogonalSliders;
		Bitboard diagonalSliders = m_FriendlyDiagonalSliders;

		if (m_InCheck)
		{
			othogonalSliders &= ~m_PinRays;
			diagonalSliders &= ~m_PinRays;
		}
	
		// Ortho
		while (othogonalSliders != 0)
		{
			int startSquare = BitUtil::PopLSB(othogonalSliders);
			Bitboard moveSquares = GetSlidingAttacks(startSquare, m_AllPieces, true) & moveMask;

			if (IsPinned(startSquare))
			{
				moveSquares &= s_AlignMask[startSquare][m_FriendlyKingSquare];
			}

			while (moveSquares != 0)
			{
				int targetSquare = BitUtil::PopLSB(moveSquares);
				m_LegalMoves.push(Move(
					startSquare, 
					targetSquare,
					GetPiece(startSquare),
					0,
					GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE
				));
			}
		}

		while (diagonalSliders != 0)
		{
			int startSquare = BitUtil::PopLSB(diagonalSliders);
			Bitboard moveSquares = GetSlidingAttacks(startSquare, m_AllPieces, false) & moveMask;

			if (IsPinned(startSquare))
			{
				moveSquares &= s_AlignMask[startSquare][m_FriendlyKingSquare];
			}

			while (moveSquares != 0)
			{
				int targetSquare = BitUtil::PopLSB(moveSquares);
				m_LegalMoves.push(Move(
					startSquare,
					targetSquare,
					GetPiece(startSquare),
					0,
					GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE
				));
			}
		}
	}

	void MoveGenerator::CalculateKnightMoves()
	{
		Bitboard knights = m_FriendlyKinghts & m_NotPinRays;
		Bitboard moveMask = ~m_FriendlyPieces & m_CheckRayBitmask;

		while (knights != 0)
		{
			int knightSquare = BitUtil::PopLSB(knights);
			Bitboard moveSquares = s_KnightMoveMask[knightSquare] & moveMask;

			while (moveSquares != 0)
			{
				int targetSquare = BitUtil::PopLSB(moveSquares);
				m_LegalMoves.push(Move(
					knightSquare, 
					targetSquare, 
					GetPiece(knightSquare),
					0,
					GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : (uint8_t)MoveFlags::IS_CAPTURE
				));
			}
		}
	}

	void MoveGenerator::CalculatePawnMoves()
	{
		int pushDir = m_WhiteToMove ? 1 : -1;
		int pushOffset = pushDir * 8;

		PieceType friendlyPawnPiece = m_WhiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN;
		Bitboard pawns = m_FriendlyPawns;
		
		Bitboard promotionRankMask = m_WhiteToMove ? Square::Rank8 : Square::Rank1;

		Bitboard singlePush = (BitUtil::Shift(pawns, pushOffset)) & ~m_AllPieces;

		Bitboard pushPromotions = singlePush & promotionRankMask & m_CheckRayBitmask;


		Bitboard captureEdgeFileMask = m_WhiteToMove ? Square::NotAFile : Square::NotHFile;
		Bitboard captureEdgeFileMask2 = m_WhiteToMove ? Square::NotHFile : Square::NotAFile;
		Bitboard captureA = BitUtil::Shift(pawns & captureEdgeFileMask, pushDir * 7) & m_OpponentPieces;
		Bitboard captureB = BitUtil::Shift(pawns & captureEdgeFileMask2, pushDir * 9) & m_OpponentPieces;

		Bitboard singlePushNoPromotions = singlePush & ~promotionRankMask & m_CheckRayBitmask;

		Bitboard capturePromotionsA = captureA & promotionRankMask & m_CheckRayBitmask;
		Bitboard capturePromotionsB = captureB & promotionRankMask & m_CheckRayBitmask;

		captureA &= m_CheckRayBitmask & ~promotionRankMask;
		captureB &= m_CheckRayBitmask & ~promotionRankMask;


		while (singlePushNoPromotions != 0)
		{
			int targetSquare = BitUtil::PopLSB(singlePushNoPromotions);
			int startSquare = targetSquare - pushOffset;
			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				m_LegalMoves.push(Move(
					startSquare,
					targetSquare,
					friendlyPawnPiece
				));
			}
		}

		Bitboard doublePushTargetRankMask = m_WhiteToMove ? Square::Rank4 : Square::Rank5;
		Bitboard doublePush = BitUtil::Shift(singlePush, pushOffset) & ~m_AllPieces & doublePushTargetRankMask & m_CheckRayBitmask;

		while (doublePush != 0)
		{
			uint8_t targetSquare = BitUtil::PopLSB(doublePush);
			uint8_t startSquare = targetSquare - pushOffset * 2;
			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				m_LegalMoves.push(Move(
					startSquare,
					targetSquare, 
					friendlyPawnPiece,
					0,
					MoveFlags::PAWN_TWO_UP
				));
			}
		}
	

		// Captures
		while (captureA != 0)	
		{
			uint8_t targetSquare = BitUtil::PopLSB(captureA);
			uint8_t startSquare = targetSquare - pushDir * 7;

			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				m_LegalMoves.push(Move(
					startSquare, 
					targetSquare,
					friendlyPawnPiece,
					0,
					MoveFlags::IS_CAPTURE
				));
			}
		}

		while (captureB != 0)
		{
			uint8_t targetSquare = BitUtil::PopLSB(captureB);
			uint8_t startSquare = targetSquare - pushDir * 9;

			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				m_LegalMoves.push(Move(
					startSquare,
					targetSquare,
					friendlyPawnPiece,
					0,
					MoveFlags::IS_CAPTURE
				));
			}
		}



		// Promotions
		while (pushPromotions != 0)
		{
			uint8_t targetSquare = BitUtil::PopLSB(pushPromotions);
			uint8_t startSquare = targetSquare - pushOffset;
			if (!IsPinned(startSquare))
			{
				GeneratePromotions(startSquare, targetSquare);
			}
		}


		while (capturePromotionsA != 0)
		{
			uint8_t targetSquare = BitUtil::PopLSB(capturePromotionsA);
			uint8_t startSquare = targetSquare - pushDir * 7;

			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				GeneratePromotions(startSquare, targetSquare);
			}
		}

		while (capturePromotionsB != 0)
		{
			uint8_t targetSquare = BitUtil::PopLSB(capturePromotionsB);
			uint8_t startSquare = targetSquare - pushDir * 9;

			if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
			{
				GeneratePromotions(startSquare, targetSquare);
			}
		}

		// En passant
		if (m_BoardState.enPassantFile < 8)
		{
			int epFileIndex = m_BoardState.enPassantFile;
			int epRankIndex = m_WhiteToMove ? 5 : 2;
			uint8_t targetSquare = epRankIndex * 8 + epFileIndex;
			Square capturedPawnSquare = targetSquare - pushOffset;

			if (capturedPawnSquare.ContainsSquare(m_CheckRayBitmask))
			{
				Bitboard pawnsThatCanCaptureEp = pawns & PawnAttacks(1ULL << targetSquare, !m_WhiteToMove);

				while (pawnsThatCanCaptureEp != 0)
				{
					uint8_t startSquare = BitUtil::PopLSB(pawnsThatCanCaptureEp);
					if (!IsPinned(startSquare) || s_AlignMask[startSquare][m_FriendlyKingSquare] == s_AlignMask[targetSquare][m_FriendlyKingSquare])
					{
						if (!InCheckAfterEnPassant(startSquare, targetSquare, capturedPawnSquare))
						{
							m_LegalMoves.push(Move(
								startSquare, 
								targetSquare, 
								friendlyPawnPiece,
								0,
								MoveFlags::IS_EN_PASSANT | MoveFlags::IS_CAPTURE
							));
						}
					}
				}
			}
		}
	
	
	}

	void MoveGenerator::GeneratePromotions(Square startSquare, Square targetSquare)
	{

		m_LegalMoves.push(Move(
			startSquare,
			targetSquare,
			m_WhiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN,
			m_WhiteToMove ? PieceType::WHITE_QUEEN : PieceType::BLACK_QUEEN,
			MoveFlags::IS_PROMOTION | (GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE)
		));

		m_LegalMoves.push(Move(
			startSquare,
			targetSquare,
			m_WhiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN,
			m_WhiteToMove ? PieceType::WHITE_ROOK : PieceType::BLACK_ROOK,
			MoveFlags::IS_PROMOTION | (GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE)
		));

		m_LegalMoves.push(Move(
			startSquare, 
			targetSquare, 
			m_WhiteToMove ? PieceType::WHITE_PAWN   : PieceType::BLACK_PAWN ,
			m_WhiteToMove ? PieceType::WHITE_BISHOP : PieceType::BLACK_BISHOP ,
			MoveFlags::IS_PROMOTION | (GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE)
		));

		m_LegalMoves.push(Move(
			startSquare,
			targetSquare,
			m_WhiteToMove ? PieceType::WHITE_PAWN : PieceType::BLACK_PAWN,
			m_WhiteToMove ? PieceType::WHITE_KNIGHT : PieceType::BLACK_KNIGHT,
			MoveFlags::IS_PROMOTION | (GetPiece(targetSquare) == PieceType::NO_PIECE ? 0 : MoveFlags::IS_CAPTURE)
		));


	}

	Bitboard MoveGenerator::GetSlidingAttacks(Square square, Bitboard blockers, bool orthogonal)
	{
		if (square >= 64)
		{
			return 0ULL;
		}
		if (orthogonal)
		{
			blockers &= s_RookMasks[square];
			unsigned long long index = (uint64_t)square * s_MaxPossibleRookMasks + ((blockers * s_RookMagics[square]) >> s_RookShifts[square]);
			return s_RookMoveMasksArray[index];
		}
		else
		{
			blockers &= s_BishopMasks[square];
			unsigned long long index = (uint64_t)square * s_MaxPossibleBishopMasks + ((blockers * s_BishopMagics[square]) >> s_BishopShifts[square]);
			return s_BishopMoveMasksArray[index];
		}
	}

	Piece MoveGenerator::GetPiece(Square square)
	{

		for (uint8_t board = 0; board < 12; board++)
		{
			if (m_BoardState.pieceBitboards[board] & 1ULL << square)
			{
				return (PieceType)board;
			}
		}

		return PieceType::NO_PIECE;
	}

	bool MoveGenerator::IsPinned(Square square) const
	{
		return ((m_PinRays >> square) & 1ULL) != 0;
	}

	bool MoveGenerator::InCheckAfterEnPassant(Square startSquare, Square targetSquare, Square epCaptureSquare)
	{
		Bitboard enemyOrtho = m_OpponentOrthogonalSliders;

		if (enemyOrtho != 0)
		{
			Bitboard maskedBlockers = (m_AllPieces ^ (1ULL << epCaptureSquare | 1ULL << startSquare | 1ULL << targetSquare));
			Bitboard rookAttacks = GetSlidingAttacks(m_FriendlyKingSquare, maskedBlockers, true);
			return (rookAttacks & enemyOrtho) != 0;
		}

		return false;
	}

	Bitboard MoveGenerator::PawnAttacks(Bitboard pawns, bool isWhite)
	{

		if (isWhite)
		{
			return ((pawns << 9) & Square::NotAFile) | ((pawns << 7) & Square::NotHFile);
		}

		return ((pawns >> 7) & Square::NotAFile) | ((pawns >> 9) & Square::NotHFile);
	}

} // namespace ChessCore