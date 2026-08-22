#include "Network.h"

#include "ChessUtil.h"
#include "Quantization.h"
#include "SimdOps.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace NeraChessNNUE
{
    using namespace NeraChessEngine;

    void Network::AllocateForCurrentArchitecture()
    {
        m_FeatureWeights.assign(Architecture::FeatureWeightCount, 0);
        m_FeatureBias.assign(Architecture::FeatureBiasCount, 0);
        m_OutputWeights.assign(Architecture::OutputWeightCount, 0);
        m_OutputBias.assign(Architecture::OutputBiasCount, 0);
    }

    void Network::Unload()
    {
        m_Loaded = false;
        m_Header = {};
        m_FeatureWeights.clear();
        m_FeatureBias.clear();
        m_OutputWeights.clear();
        m_OutputBias.clear();
    }

    NetworkFormat::Status Network::LoadFromMemory(std::span<const std::byte> data)
    {
        NetworkFormat::Header header;
        const NetworkFormat::Status status = NetworkFormat::ReadHeader(data, header);
        if (status != NetworkFormat::Status::Ok)
        {
            Unload();
            return status;
        }

        const size_t payloadBytes = static_cast<size_t>(header.parameterCount) * sizeof(Weight);
        const std::span<const std::byte> payload =
            data.subspan(NetworkFormat::HeaderBytes, payloadBytes);
        if (NetworkFormat::Checksum(payload) != header.checksum)
        {
            Unload();
            return NetworkFormat::Status::ChecksumMismatch;
        }

        AllocateForCurrentArchitecture();

        size_t cursor = 0;
        const auto readInto = [&payload, &cursor](std::vector<Weight>& destination)
        {
            for (Weight& value : destination)
                value = NetworkFormat::ReadWeight(payload, cursor++);
        };
        readInto(m_FeatureWeights);
        readInto(m_FeatureBias);
        readInto(m_OutputWeights);
        readInto(m_OutputBias);

        m_Header = header;
        m_Loaded = true;
        return NetworkFormat::Status::Ok;
    }

    NetworkFormat::Status Network::LoadFromFile(const std::filesystem::path& path)
    {
        std::error_code error;
        if (path.empty() || !std::filesystem::exists(path, error) || error)
        {
            Unload();
            return NetworkFormat::Status::FileNotFound;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            Unload();
            return NetworkFormat::Status::ReadFailed;
        }

        const std::vector<char> raw{ std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>() };
        if (stream.bad())
        {
            Unload();
            return NetworkFormat::Status::ReadFailed;
        }

        return LoadFromMemory(std::as_bytes(std::span<const char>(raw)));
    }

    NetworkFormat::Status Network::SaveToFile(const std::filesystem::path& path) const
    {
        if (!m_Loaded)
            return NetworkFormat::Status::NotLoaded;

        std::vector<std::byte> payload(Architecture::TotalParameterBytes, std::byte{ 0 });
        size_t cursor = 0;
        const auto writeFrom = [&payload, &cursor](const std::vector<Weight>& source)
        {
            for (const Weight value : source)
                NetworkFormat::WriteWeight(payload, cursor++, value);
        };
        writeFrom(m_FeatureWeights);
        writeFrom(m_FeatureBias);
        writeFrom(m_OutputWeights);
        writeFrom(m_OutputBias);

        NetworkFormat::Header header = NetworkFormat::Header::ForCurrentArchitecture();
        header.checksum = NetworkFormat::Checksum(payload);

        std::vector<std::byte> headerBytes(NetworkFormat::HeaderBytes, std::byte{ 0 });
        NetworkFormat::WriteHeader(header, headerBytes);

        // Write beside the destination and rename over it. Truncating the
        // target first means an interrupted save leaves a half-written file
        // where a valid network used to be, which the next run loads as a
        // corrupt network rather than noticing the save never finished.
        std::filesystem::path temporary = path;
        temporary += ".partial";

        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                return NetworkFormat::Status::WriteFailed;

            stream.write(reinterpret_cast<const char*>(headerBytes.data()),
                static_cast<std::streamsize>(headerBytes.size()));
            stream.write(reinterpret_cast<const char*>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
            stream.flush();
            if (!stream)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return NetworkFormat::Status::WriteFailed;
            }
        }

        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return NetworkFormat::Status::WriteFailed;
        }

        return NetworkFormat::Status::Ok;
    }

    size_t Network::OutputBucketOf(const BoardState& state)
    {
        if constexpr (Architecture::OutputBucketCount == 1)
        {
            (void)state;
            return 0;
        }
        else
        {
            // TODO(nnue): bucket by total piece count once more than one head
            // exists, using the same formula as the trainer.
            Bitboard occupancy = 0;
            for (const Bitboard pieces : state.pieceBitboards)
                occupancy |= pieces;
            const size_t pieceCount = BitUtil::PopCnt(occupancy);
            return (pieceCount - 2) * Architecture::OutputBucketCount / 30;
        }
    }

    Score Network::Forward(const Accumulator& accumulator, Perspective sideToMove,
        size_t outputBucket) const
    {
        if (!m_Loaded)
            return 0;

        const size_t bucket = outputBucket < Architecture::OutputBucketCount ? outputBucket : 0;
        const Weight* weights = m_OutputWeights.data() + bucket * Architecture::OutputInputSize;

        // The side to move always occupies the first half of the output layer's
        // input, so the network never has to learn the same pattern twice.
        const Accumulation own = Simd::ActivatedDotProduct(accumulator[sideToMove],
            weights, Architecture::HiddenSize);
        const Accumulation opponent = Simd::ActivatedDotProduct(accumulator[Opposite(sideToMove)],
            weights + Architecture::HiddenSize, Architecture::HiddenSize);

        return Quantization::Dequantize(own + opponent, m_OutputBias[bucket]);
    }
}
