#pragma once

#include <string>
#include <chrono>
#include <unordered_map>

#include "onnxruntime_cxx_api.h"

#include "../ChessPlayer.h"
#include "TranspositionTable.h"

class NeraChessBot : public ChessPlayer
{
public:
	NeraChessBot(const std::string& modelPath = "model15b.onnx");
	~NeraChessBot();

	Move GetNextMove(const ChessBoard& givenBoard, Timer timer) override;

private:
	
	Move IterativeDeepeningSearch(ChessBoard& board, int maxDepth);
	Move PVSRoot(ChessBoard& board, int depth);

	float PrincipalVariationSearch(ChessBoard& board, float alpha, float beta, int depth, uint8_t ply);
	float QuiescenceSearch(ChessBoard& board, float alpha, float beta, uint8_t ply);

	float EvaluateBoard(const ChessBoard& board);
	float FastStaticEval(const ChessBoard& board);
	float EvaluateTerminal(const ChessBoard& board);

	bool PositiveSEE(const ChessBoard& board, Move move);

	void SortMoves(const ChessBoard& board, MoveList<218>& moves, uint8_t ply, Move ttMove = 0);

	std::array<float, 19 * 8 * 8> BoardToTensor(const ChessBoard& board) const;

	inline bool IsTimeUp() const { return (std::chrono::steady_clock::now() - m_SearchStartTime) >= m_TimeLimitS; }
	inline int NullMoveReduction(int depth) { return 2 + (depth >= 6 ? 1 : 0); }

private:

	// Const stuff
	static inline const int c_BoardSize = 8;
	static inline const int64_t c_NumChannels = 19;
	static inline const int64_t c_InputTensorSize = 1 * c_NumChannels * c_BoardSize * c_BoardSize;

	static inline const std::array<int64_t, 4> c_InputShape = { 1, c_NumChannels, c_BoardSize, c_BoardSize };
	static inline const std::array<int64_t, 2> c_OutputShape = { 1, 1 };

	static inline const int c_PieceValues[12] = {
		1,		// white pawn
		3,		// WHITE_KNIGHT
		3,		// WHITE_BISHOP
		5,		// WHITE_ROOK
		9,		// WHITE_QUEEN
		1000,	// WHITE_KING
		-1,		// BLACK_PAWN
		-3,		// BLACK_KNIGHT
		-3,		// BLACK_BISHOP
		-5,		// BLACK_ROOK
		-9,		// BLACK_QUEEN
		-1000	// BLACK_KING
	};

	std::unordered_map<uint64_t, float> s_EvaluationCache;

	// Timing Stuff
	std::chrono::time_point<std::chrono::steady_clock> m_SearchStartTime;
	std::chrono::seconds m_TimeLimitS{30};
	std::atomic<bool> m_TimeUp{ false };

	// AI Stuff
	Ort::Env m_Env{ ORT_LOGGING_LEVEL_WARNING, "FirstNNBot" };
	Ort::SessionOptions m_SessionOptions;
	Ort::Session m_Session{ nullptr };

	std::array<float, c_InputTensorSize> m_InputArray = {};
	Ort::MemoryInfo m_MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	const char* m_InputName = "input";
	const char* m_OutputName = "output";

	std::vector<Ort::Value> m_InputVector;

	TranspositionTable m_TranspositionTable{ 2000 }; // 500 MB

	Move m_KillerMoves[100][2] = {};
	int m_HistoryHeuristic[64][64] = {};

	uint32_t m_SearchID = 0;
};
