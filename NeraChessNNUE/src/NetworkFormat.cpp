#include "NetworkFormat.h"

#include <algorithm>
#include <string>

namespace NeraChessNNUE::NetworkFormat
{
    namespace
    {
        // Little-endian readers and writers, so a network file produced on any
        // host loads on any other.
        uint64_t ReadLittleEndian(std::span<const std::byte> data, size_t offset, size_t width)
        {
            uint64_t value = 0;
            for (size_t byte = 0; byte < width; ++byte)
                value |= static_cast<uint64_t>(std::to_integer<uint8_t>(data[offset + byte])) << (byte * 8);
            return value;
        }

        void WriteLittleEndian(std::span<std::byte> data, size_t offset, size_t width, uint64_t value)
        {
            for (size_t byte = 0; byte < width; ++byte)
                data[offset + byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xFFu);
        }
    }

    Header Header::ForCurrentArchitecture()
    {
        Header header;
        header.version = Version;
        header.architectureHash = Architecture::ArchitectureHash();
        header.perspectiveInputSize = static_cast<uint16_t>(Architecture::PerspectiveInputSize);
        header.inputBucketCount = static_cast<uint16_t>(Architecture::InputBucketCount);
        header.hiddenSize = static_cast<uint16_t>(Architecture::HiddenSize);
        header.outputBucketCount = static_cast<uint16_t>(Architecture::OutputBucketCount);
        header.quantizationA = static_cast<uint16_t>(Architecture::QuantizationA);
        header.quantizationB = static_cast<uint16_t>(Architecture::QuantizationB);
        header.evalScale = static_cast<uint16_t>(Architecture::EvalScale);
        header.activation = static_cast<uint16_t>(Architecture::Activation);
        header.parameterCount = Architecture::TotalParameterCount;
        header.checksum = 0;
        return header;
    }

    bool Header::MatchesCurrentArchitecture() const
    {
        const Header expected = ForCurrentArchitecture();
        return architectureHash == expected.architectureHash &&
            perspectiveInputSize == expected.perspectiveInputSize &&
            inputBucketCount == expected.inputBucketCount &&
            hiddenSize == expected.hiddenSize &&
            outputBucketCount == expected.outputBucketCount &&
            quantizationA == expected.quantizationA &&
            quantizationB == expected.quantizationB &&
            evalScale == expected.evalScale &&
            activation == expected.activation &&
            parameterCount == expected.parameterCount;
    }

    std::string Header::Describe() const
    {
        return std::to_string(perspectiveInputSize) + 'x' + std::to_string(inputBucketCount) +
            " -> " + std::to_string(hiddenSize) + "x2 -> 1x" + std::to_string(outputBucketCount) +
            " (qa " + std::to_string(quantizationA) +
            ", qb " + std::to_string(quantizationB) +
            ", scale " + std::to_string(evalScale) + ')';
    }

    std::string_view Describe(Status status)
    {
        switch (status)
        {
        case Status::Ok:                   return "ok";
        case Status::FileNotFound:         return "network file not found";
        case Status::ReadFailed:           return "network file could not be read";
        case Status::TooSmall:             return "network file is smaller than its header";
        case Status::BadMagic:             return "network file is not a NeraChess network";
        case Status::UnsupportedVersion:   return "network file uses an unsupported format version";
        case Status::ArchitectureMismatch: return "network file was trained for a different architecture";
        case Status::TruncatedPayload:     return "network file is truncated";
        case Status::ChecksumMismatch:     return "network file failed its checksum";
        }
        return "unknown network error";
    }

    uint64_t Checksum(std::span<const std::byte> payload)
    {
        constexpr uint64_t Prime = 1'099'511'628'211ull;
        uint64_t hash = 14'695'981'039'346'656'037ull;
        for (const std::byte value : payload)
        {
            hash ^= std::to_integer<uint8_t>(value);
            hash *= Prime;
        }
        return hash;
    }

    Status ReadHeader(std::span<const std::byte> data, Header& out)
    {
        if (data.size() < HeaderBytes)
            return Status::TooSmall;

        for (size_t index = 0; index < Magic.size(); ++index)
        {
            if (std::to_integer<uint8_t>(data[index]) != static_cast<uint8_t>(Magic[index]))
                return Status::BadMagic;
        }

        Header header;
        header.version = static_cast<uint32_t>(ReadLittleEndian(data, 8, 4));
        header.architectureHash = static_cast<uint32_t>(ReadLittleEndian(data, 12, 4));
        header.perspectiveInputSize = static_cast<uint16_t>(ReadLittleEndian(data, 16, 2));
        header.inputBucketCount = static_cast<uint16_t>(ReadLittleEndian(data, 18, 2));
        header.hiddenSize = static_cast<uint16_t>(ReadLittleEndian(data, 20, 2));
        header.outputBucketCount = static_cast<uint16_t>(ReadLittleEndian(data, 22, 2));
        header.quantizationA = static_cast<uint16_t>(ReadLittleEndian(data, 24, 2));
        header.quantizationB = static_cast<uint16_t>(ReadLittleEndian(data, 26, 2));
        header.evalScale = static_cast<uint16_t>(ReadLittleEndian(data, 28, 2));
        header.activation = static_cast<uint16_t>(ReadLittleEndian(data, 30, 2));
        header.parameterCount = ReadLittleEndian(data, 32, 8);
        header.checksum = ReadLittleEndian(data, 40, 8);
        out = header;

        if (header.version != Version)
            return Status::UnsupportedVersion;
        if (!header.MatchesCurrentArchitecture())
            return Status::ArchitectureMismatch;

        const size_t payloadBytes = static_cast<size_t>(header.parameterCount) * sizeof(Weight);
        if (data.size() - HeaderBytes < payloadBytes)
            return Status::TruncatedPayload;

        return Status::Ok;
    }

    void WriteHeader(const Header& header, std::span<std::byte> out)
    {
        if (out.size() < HeaderBytes)
            return;

        for (size_t index = 0; index < Magic.size(); ++index)
            out[index] = static_cast<std::byte>(Magic[index]);

        WriteLittleEndian(out, 8, 4, header.version);
        WriteLittleEndian(out, 12, 4, header.architectureHash);
        WriteLittleEndian(out, 16, 2, header.perspectiveInputSize);
        WriteLittleEndian(out, 18, 2, header.inputBucketCount);
        WriteLittleEndian(out, 20, 2, header.hiddenSize);
        WriteLittleEndian(out, 22, 2, header.outputBucketCount);
        WriteLittleEndian(out, 24, 2, header.quantizationA);
        WriteLittleEndian(out, 26, 2, header.quantizationB);
        WriteLittleEndian(out, 28, 2, header.evalScale);
        WriteLittleEndian(out, 30, 2, header.activation);
        WriteLittleEndian(out, 32, 8, header.parameterCount);
        WriteLittleEndian(out, 40, 8, header.checksum);
    }

    Weight ReadWeight(std::span<const std::byte> payload, size_t index)
    {
        const uint16_t raw = static_cast<uint16_t>(
            ReadLittleEndian(payload, index * sizeof(Weight), sizeof(Weight)));
        return static_cast<Weight>(raw);
    }

    void WriteWeight(std::span<std::byte> payload, size_t index, Weight value)
    {
        WriteLittleEndian(payload, index * sizeof(Weight), sizeof(Weight),
            static_cast<uint16_t>(value));
    }
}
