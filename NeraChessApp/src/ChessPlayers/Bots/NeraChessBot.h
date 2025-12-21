#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <fstream>

#include "onnxruntime_cxx_api.h"

#include "../ChessPlayer.h"
#include "TranspositionTable.h"

#include <atomic>

class NeraChessBot : public ChessPlayer
{
public:
	NeraChessBot(const std::string& modelPath = "Ressources/NeuralNetworks/model15b.onnx");
	~NeraChessBot();

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& givenBoard, const ChessCore::Clock& timer) override;
	virtual void ResetGame() override { m_OpeningBookAvailable = true; m_StopSearching = false; };
	virtual void StopSearching() override { m_StopSearching = true; };

private:
	
	ChessCore::Move GetOpeningBookMove(const ChessCore::ChessBoard& board);

	ChessCore::Move IterativeDeepeningSearch(ChessCore::ChessBoard& board, int maxDepth);
	ChessCore::Move PVSRoot(ChessCore::ChessBoard& board, int depth);

	float PrincipalVariationSearch(ChessCore::ChessBoard& board, float alpha, float beta, int depth, uint8_t ply);
	float QuiescenceSearch(ChessCore::ChessBoard& board, float alpha, float beta, uint8_t ply);

	float EvaluateBoard(const ChessCore::ChessBoard& board);
	float FastStaticEval(const ChessCore::ChessBoard& board);
	float EvaluateTerminal(const ChessCore::ChessBoard& board);

	bool PositiveSEE(const ChessCore::ChessBoard& board, ChessCore::Move move);

	void SortMoves(const ChessCore::ChessBoard& board, ChessCore::MoveList<218>& moves, uint8_t ply, ChessCore::Move ttMove = 0);

	std::array<float, 19 * 8 * 8> BoardToTensor(const ChessCore::ChessBoard& board) const;

	int LateMoveReduction(int depth, uint8_t ply, uint8_t moveIndex) const;

	bool IsTimeUp();
	inline int NullMoveReduction(int depth) { return 2 + (depth >= 6 ? 1 : 0); }


private:

	// Const stuff
	static inline const int c_BoardSize = 8;
	static inline const int64_t c_NumChannels = 19;
	static inline const int64_t c_InputTensorSize = 1 * c_NumChannels * c_BoardSize * c_BoardSize;

	static inline const std::array<int64_t, 4> c_InputShape = { 1, c_NumChannels, c_BoardSize, c_BoardSize };
	static inline const std::array<int64_t, 2> c_OutputShape = { 1, 1 };

	static inline const float c_PieceValues[12] = {
		1.f,		// white pawn
		3.f,		// WHITE_KNIGHT
		3.2f,		// WHITE_BISHOP
		5.f,		// WHITE_ROOK
		9.f,		// WHITE_QUEEN
		1000.f,	// WHITE_KING
		-1.f,		// BLACK_PAWN
		-3.f,		// BLACK_KNIGHT
		-3.2f,		// BLACK_BISHOP
		-5.f,		// BLACK_ROOK
		-9.f,		// BLACK_QUEEN
		-1000.f	// BLACK_KING
	};

	static inline const std::string c_OpeningBookPath = "Ressources/OpeningBook/OpeningBook.txt";
	std::ifstream m_OpeningBook;

	// Cache
	std::unordered_map<uint64_t, float> s_EvaluationCache;

	// Timing Stuff
	std::chrono::time_point<std::chrono::steady_clock> m_SearchStartTime;
	std::chrono::seconds m_TimeLimitS{ 15 };
	std::atomic<bool> m_TimeUp{ false };

	// AI Stuff
	Ort::Env m_Env{ ORT_LOGGING_LEVEL_WARNING, "NeraChessBot" };
	Ort::SessionOptions m_SessionOptions;
	OrtCUDAProviderOptions m_CudaOptions;
	Ort::Session m_Session{ nullptr };

	std::array<float, c_InputTensorSize> m_InputArray = {};
	Ort::MemoryInfo m_MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	const char* m_InputName = "input";
	const char* m_OutputName = "output";

	std::vector<Ort::Value> m_InputVector;

	// Transpotision Table
	TranspositionTable m_TranspositionTable{ 256 }; // 256 MB

	// Search Heuristics
	ChessCore::Move m_KillerMoves[100][2] = {};
	int m_HistoryHeuristic[64][64] = {};

	// Opening book
	bool m_OpeningBookAvailable = true;

	// Debug Info
	uint64_t m_NodesSearched = 0;
	uint64_t m_QuiescenceNodesSearched = 0;
	uint64_t m_NodesEvaluated = 0;

	uint64_t m_NodesAtDepth[200] = {};

	// other
	uint32_t m_SearchID = 0;
	std::atomic<bool> m_StopSearching;

};

