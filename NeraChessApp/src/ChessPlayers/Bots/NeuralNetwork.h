#pragma once

#include "ChessBoard.h"

#include "onnxruntime_cxx_api.h"

#include <string>

class NeuralNetwork
{
public:
	NeuralNetwork(const std::string& modelPath);
	~NeuralNetwork();

	float GetEvaluation(const ChessCore::ChessBoard& board);

	void QueuePosition(const ChessCore::ChessBoard& board);

private:

	void BoardToTensor(const ChessCore::ChessBoard& board, float* out) const;

	void EvaluateQueue();

private:

	struct BoardInfo
	{
		uint64_t ZobristKey{ 0 };
		bool WhiteToMove{ true };
	};

	// Const stuff
	static inline const int c_BoardSize = 8;
	static inline const int64_t c_NumChannels = 19;
	static inline const int64_t c_InputTensorSize = 1 * c_NumChannels * c_BoardSize * c_BoardSize;

	std::array<int64_t, 4> m_InputShape = { 1, c_NumChannels, c_BoardSize, c_BoardSize };
	std::array<int64_t, 2> m_OutputShape = { 1, 1 };

	const char* m_InputName = "input";
	const char* m_OutputName = "output";

	uint8_t m_BatchSize = 32;

	// Ort
	Ort::Env m_Env{ ORT_LOGGING_LEVEL_WARNING, "NeraChessBot" };
	Ort::SessionOptions m_SessionOptions;
	OrtCUDAProviderOptions m_CudaOptions;
	Ort::Session m_Session{ nullptr };
	Ort::MemoryInfo m_MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	// Data
	std::vector<float> m_InputBuffer;
	std::array<float, c_InputTensorSize> m_InputArray = {};

	std::vector<BoardInfo> m_InfoVector;

	// Cache
	std::unordered_map<uint64_t, float> s_EvaluationCache;

};
