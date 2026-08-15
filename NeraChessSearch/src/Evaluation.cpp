#include "Evaluation.h"

#include "NnueEvaluator.h"

namespace NeraChessSearch::Evaluation
{
    using namespace NeraChessEngine;
    namespace Nnue = NeraChessNNUE;

    int32_t Evaluate(const ChessBoard& board)
    {
        return Nnue::Evaluator::Evaluate(board);
    }

    int32_t Evaluate(const BoardState& state, Nnue::Accumulator& accumulator)
    {
        return Nnue::Evaluator::Evaluate(state, accumulator);
    }

    bool IsNetworkLoaded()
    {
        return Nnue::Evaluator::IsLoaded();
    }

    bool LoadNetwork(const std::filesystem::path& path, std::string& message)
    {
        const Nnue::NetworkFormat::Status status = Nnue::Evaluator::Load(path);
        if (status == Nnue::NetworkFormat::Status::Ok)
        {
            message = StatusText();
            return true;
        }

        message = "failed to load " + path.string() + ": " +
            std::string(Nnue::NetworkFormat::Describe(status));
        return false;
    }

    void LoadDefaultNetwork(const std::filesystem::path& executableDirectory)
    {
        Nnue::Evaluator::LoadDefault(executableDirectory);
    }

    std::string StatusText()
    {
        return Nnue::Evaluator::StatusText();
    }
}
