#pragma once

#include "NetworkArchitecture.h"
#include "NnueCommon.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// On-disk layout of a ".nnue" network file.
//
// The format is explicitly little-endian and self-describing: a file records
// the architecture it was trained for, so loading a mismatched network fails
// loudly instead of producing plausible-looking nonsense.
//
//   offset  size  field
//   0       8     magic, "NERANNUE"
//   8       4     format version
//   12      4     architecture hash (Architecture::ArchitectureHash())
//   16      2     perspective input size
//   18      2     input bucket count
//   20      2     hidden size
//   22      2     output bucket count
//   24      2     quantization A
//   26      2     quantization B
//   28      2     eval scale
//   30      2     activation kind
//   32      8     parameter count
//   40      8     FNV-1a checksum of the parameter payload
//   48      ...   parameters, int16 little-endian
//
// Payload order, matching Network's member order:
//
//   1. feature weights   [input bucket][feature][neuron]
//   2. feature biases    [neuron]
//   3. output weights    [output bucket][2 * neuron]   own perspective first
//   4. output biases     [output bucket]
//
// The writer lives in NNUETraining/nnue_training/serialize.py and must be
// kept in step with this file.

namespace NeraChessNNUE::NetworkFormat
{
    inline constexpr std::array<char, 8> Magic = { 'N', 'E', 'R', 'A', 'N', 'N', 'U', 'E' };

    // Bumped only for changes to the container itself. Changes to the network
    // shape are caught by the architecture hash instead.
    inline constexpr uint32_t Version = 1;

    inline constexpr size_t HeaderBytes = 48;

    struct Header
    {
        uint32_t version = Version;
        uint32_t architectureHash = 0;
        uint16_t perspectiveInputSize = 0;
        uint16_t inputBucketCount = 0;
        uint16_t hiddenSize = 0;
        uint16_t outputBucketCount = 0;
        uint16_t quantizationA = 0;
        uint16_t quantizationB = 0;
        uint16_t evalScale = 0;
        uint16_t activation = 0;
        uint64_t parameterCount = 0;
        uint64_t checksum = 0;

        // The header this build expects, with checksum left for the caller.
        static Header ForCurrentArchitecture();

        // Whether this header describes the architecture compiled into the
        // engine. Compares every field rather than only the hash so that a
        // mismatch can be reported in terms a human can act on.
        bool MatchesCurrentArchitecture() const;

        // Human-readable "768x1 -> 512x2 -> 1x1" style summary, for UCI info
        // strings and error messages.
        std::string Describe() const;
    };

    enum class Status : uint8_t
    {
        Ok = 0,
        FileNotFound,
        ReadFailed,
        TooSmall,
        BadMagic,
        UnsupportedVersion,
        ArchitectureMismatch,
        TruncatedPayload,
        ChecksumMismatch,
        // Saving only. Reporting a write problem as ReadFailed sent whoever
        // read the message looking at the wrong end of the operation.
        NotLoaded,
        WriteFailed,
    };

    std::string_view Describe(Status status);

    // FNV-1a over the parameter payload. Cheap, and enough to catch a
    // truncated download or a file written by a mismatched exporter.
    uint64_t Checksum(std::span<const std::byte> payload);

    // Parses and validates the 48-byte header at the start of data. Does not
    // touch the payload beyond checking that it is long enough.
    Status ReadHeader(std::span<const std::byte> data, Header& out);

    // Serializes a header into exactly HeaderBytes bytes.
    void WriteHeader(const Header& header, std::span<std::byte> out);

    // Little-endian int16 payload accessors, used by the loader and by tests
    // that build a network in memory.
    Weight ReadWeight(std::span<const std::byte> payload, size_t index);
    void WriteWeight(std::span<std::byte> payload, size_t index, Weight value);
}
