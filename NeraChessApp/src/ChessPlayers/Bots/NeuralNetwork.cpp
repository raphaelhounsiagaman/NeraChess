#include "NeuralNetwork.h"

#include <filesystem>
#include <print>
#include <thread>

NeuralNetwork::NeuralNetwork(const std::string& modelPath)
{
	if (std::filesystem::exists(modelPath))
		std::println("Model found");
	else
	{
		std::println("Model missing ({})", modelPath);
		assert(false);
		return;
	}

	// Initialization of OnnxRuntime
	std::wstring wide(modelPath.begin(), modelPath.end());
	const wchar_t* wmodelPath = wide.c_str();
	m_SessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	m_SessionOptions.SetIntraOpNumThreads(std::thread::hardware_concurrency());

	//m_CudaOptions.arena_extend_strategy = 0;
	//m_CudaOptions.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
	//m_CudaOptions.do_copy_in_default_stream = 1;
	//
	//m_SessionOptions.AppendExecutionProvider_CUDA(m_CudaOptions);
	m_Session = Ort::Session(m_Env, wmodelPath, m_SessionOptions);

	m_InputBuffer.resize(m_BatchSize * c_InputTensorSize);
	m_InfoVector.reserve(m_BatchSize);

	//ChessCore::ChessBoard board{"r1b1kb1r/1pp2ppp/p1p2n2/8/3qP3/P1N2N2/1PP2PPP/R1B1K2R w KQkq - 0 9"};

	//std::print("Evaluation of position: {}", GetEvaluation(board));



}

NeuralNetwork::~NeuralNetwork()
{
	m_Session.release();
	m_Env.release();
}

float NeuralNetwork::GetEvaluation(const ChessCore::ChessBoard& board)
{
	QueuePosition(board);
	EvaluateQueue();
	return s_EvaluationCache[board.GetZobristKey()];
}

void NeuralNetwork::QueuePosition(const ChessCore::ChessBoard& board)
{
	for (BoardInfo& info : m_InfoVector)
	{
		if (info.ZobristKey == board.GetZobristKey())
			return;
	}

	auto cacheIt = s_EvaluationCache.find(board.GetZobristKey());
	if (cacheIt != s_EvaluationCache.end())
		return;

	float* pos = &m_InputBuffer[m_InfoVector.size() * c_InputTensorSize];

	BoardToTensor(board, pos);

	m_InfoVector.emplace_back(board.GetZobristKey(), board.GetBoardState().HasFlag(ChessCore::BoardStateFlags::WhiteToMove));

	if (m_InfoVector.size() >= m_BatchSize)
		EvaluateQueue();
}

void NeuralNetwork::BoardToTensor(const ChessCore::ChessBoard& board, float* out) const
{

	const ChessCore::BoardState& boardState = board.GetBoardState();

	for (ChessCore::Square square = 0; square < 64; square++)
	{
		ChessCore::Piece piece = board.GetPiece(square);
		if (piece != ChessCore::PieceType::NO_PIECE)
		{
			uint8_t file = square.GetFile();
			uint8_t rank = square.GetRank();

			out[piece * 64 + file * 8 + rank] = 1.0f;
		}
	}

	if (boardState.HasFlag(ChessCore::BoardStateFlags::WhiteToMove))
	{
		float* ptr = &out[12 * 64];
		std::fill(ptr, ptr + 64, 1.0f);
	}

	auto fill_castle = [&](int channel)
		{
			float* ptr = &out[channel * 64];
			std::fill(ptr, ptr + 64, 1.0f);
		};

	if (boardState.HasFlag(ChessCore::BoardStateFlags::CanWhiteCastleKing)) fill_castle(13);
	if (boardState.HasFlag(ChessCore::BoardStateFlags::CanWhiteCastleQueen)) fill_castle(14);
	if (boardState.HasFlag(ChessCore::BoardStateFlags::CanBlackCastleKing)) fill_castle(15);
	if (boardState.HasFlag(ChessCore::BoardStateFlags::CanBlackCastleQueen)) fill_castle(16);

	// en passant
	if (boardState.HasFlag(ChessCore::BoardStateFlags::CanEnPassent))
	{
		uint8_t file = boardState.enPassantFile;
		if (file >= 0 && file <= 7)
		{
			float* ptr = &out[17 * 64 + file * 8];
			std::fill(ptr, ptr + 8, 1.0f); // fill only that file
		}
	}

	// halfmove clock normalized
	float val = static_cast<float>(board.GetHalfMoveClock()) / 50.0f;
	float* ptr = &out[18 * 64];
	std::fill(ptr, ptr + 64, val);

}

void NeuralNetwork::EvaluateQueue()
{
	if (m_InfoVector.size() == 0)
		return;

	m_InputShape[0] = m_InfoVector.size();

	Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
		m_MemoryInfo,
		m_InputBuffer.data(), m_InfoVector.size() * c_InputTensorSize,
		m_InputShape.data(), m_InputShape.size()
	);

	const char* output_names[] = { "output" };

	std::vector<Ort::Value> outputVector = m_Session.Run(
		Ort::RunOptions{ nullptr },
		&m_InputName, &inputTensor, 1,
		&m_OutputName,	1
	);

	const float* const eval = outputVector.front().GetTensorData<float>();

	for (int i{ 0 }; i < m_InfoVector.size(); i++)
	{
		const float perspectiveEval = eval[i] * float(m_InfoVector[i].WhiteToMove ? 1.f : -1.f);

		s_EvaluationCache[m_InfoVector[i].ZobristKey] = perspectiveEval;
	}

	m_InfoVector.clear();
}
