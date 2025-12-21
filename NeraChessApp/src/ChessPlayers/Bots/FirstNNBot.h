#pragma once
#include <string>

#include "onnxruntime_cxx_api.h"

#include "../ChessPlayer.h"

class FirstNNBot : public ChessPlayer
{
public:
	FirstNNBot(const std::string& modelPath = "Ressources/NeuralNetworks/model15b.onnx");
	~FirstNNBot();

	virtual ChessCore::Move GetNextMove(const ChessCore::ChessBoard& givenBoard, const ChessCore::Clock& timer) override;
	virtual void ResetGame() override {};
	virtual void StopSearching() override {};

private:

	float MinimaxSearch(ChessCore::ChessBoard& board, int depth, bool whiteMaximizingPlayer, float alpha, float beta);

	float EvaluateBoard(const ChessCore::ChessBoard& board, bool whiteToMove);

	static void SortMoves(const ChessCore::ChessBoard& board, ChessCore::MoveList<218>& moves);

	std::array<float, 19 * 8 * 8> BoardToTensor(const ChessCore::ChessBoard& board) const;

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
	
	// AI Stuff
	Ort::Env m_Env{ ORT_LOGGING_LEVEL_WARNING, "FirstNNBot" };
	Ort::SessionOptions m_SessionOptions;
	Ort::Session m_Session{nullptr};

	std::array<float, c_InputTensorSize> m_InputArray = {};
	Ort::MemoryInfo m_MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	const char* m_InputName = "input";
	const char* m_OutputName = "output";
	
	std::vector<Ort::Value> m_InputVector;
};
