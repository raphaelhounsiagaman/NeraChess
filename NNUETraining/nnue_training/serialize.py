"""Reader and writer for the ``.nnue`` container.

Mirrors NeraChessNNUE/src/NetworkFormat.{h,cpp}. Deliberately depends on
nothing but the standard library, so the format can be inspected and tested
without installing PyTorch.

Layout (little-endian throughout)::

    offset  size  field
    0       8     magic, b"NERANNUE"
    8       4     format version
    12      4     architecture hash
    16      2     perspective input size
    18      2     input bucket count
    20      2     hidden size
    22      2     output bucket count
    24      2     quantization A
    26      2     quantization B
    28      2     eval scale
    30      2     activation kind
    32      8     parameter count
    40      8     FNV-1a checksum of the parameter payload
    48      ...   parameters, int16

Payload order::

    1. feature weights   [input bucket][feature][neuron]
    2. feature biases    [neuron]
    3. output weights    [output bucket][2 * neuron]   own perspective first
    4. output biases     [output bucket]
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from . import architecture as arch
from .atomic import atomic_write_bytes

MAGIC = b"NERANNUE"
VERSION = 1
HEADER_BYTES = 48

_HEADER_STRUCT = struct.Struct("<8sIIHHHHHHHHQQ")
assert _HEADER_STRUCT.size == HEADER_BYTES


class NetworkFormatError(ValueError):
    """Raised when a file is not a usable network for this architecture."""


@dataclass(frozen=True)
class Header:
    version: int
    architecture_hash: int
    perspective_input_size: int
    input_bucket_count: int
    hidden_size: int
    output_bucket_count: int
    quantization_a: int
    quantization_b: int
    eval_scale: int
    activation: int
    parameter_count: int
    checksum: int

    @staticmethod
    def for_current_architecture(checksum: int = 0) -> "Header":
        return Header(
            version=VERSION,
            architecture_hash=arch.architecture_hash(),
            perspective_input_size=arch.PERSPECTIVE_INPUT_SIZE,
            input_bucket_count=arch.INPUT_BUCKET_COUNT,
            hidden_size=arch.HIDDEN_SIZE,
            output_bucket_count=arch.OUTPUT_BUCKET_COUNT,
            quantization_a=arch.QUANTIZATION_A,
            quantization_b=arch.QUANTIZATION_B,
            eval_scale=arch.EVAL_SCALE,
            activation=int(arch.ACTIVATION),
            parameter_count=arch.TOTAL_PARAMETER_COUNT,
            checksum=checksum,
        )

    def matches_current_architecture(self) -> bool:
        expected = Header.for_current_architecture(self.checksum)
        return self == expected


@dataclass(frozen=True)
class NetworkParameters:
    """Quantized parameters, flat and in payload order."""

    feature_weights: Sequence[int]
    feature_bias: Sequence[int]
    output_weights: Sequence[int]
    output_bias: Sequence[int]

    def validate(self) -> None:
        expected = (
            ("feature_weights", self.feature_weights, arch.FEATURE_WEIGHT_COUNT),
            ("feature_bias", self.feature_bias, arch.FEATURE_BIAS_COUNT),
            ("output_weights", self.output_weights, arch.OUTPUT_WEIGHT_COUNT),
            ("output_bias", self.output_bias, arch.OUTPUT_BIAS_COUNT),
        )
        for name, values, count in expected:
            if len(values) != count:
                raise NetworkFormatError(
                    f"{name} has {len(values)} entries, expected {count}"
                )
            for value in values:
                if not -32768 <= int(value) <= 32767:
                    raise NetworkFormatError(
                        f"{name} contains {value}, which does not fit in an int16; "
                        "the network needs stronger weight clipping during training"
                    )

    def flat(self) -> list[int]:
        return [
            *map(int, self.feature_weights),
            *map(int, self.feature_bias),
            *map(int, self.output_weights),
            *map(int, self.output_bias),
        ]


def checksum(payload: bytes) -> int:
    """FNV-1a over the parameter payload, matching NetworkFormat::Checksum."""
    prime = 1_099_511_628_211
    mask = 0xFFFFFFFFFFFFFFFF
    digest = 14_695_981_039_346_656_037
    for byte in payload:
        digest ^= byte
        digest = (digest * prime) & mask
    return digest


def pack_payload(parameters: NetworkParameters) -> bytes:
    parameters.validate()
    values = parameters.flat()
    return struct.pack(f"<{len(values)}h", *values)


def pack_header(header: Header) -> bytes:
    return _HEADER_STRUCT.pack(
        MAGIC,
        header.version,
        header.architecture_hash,
        header.perspective_input_size,
        header.input_bucket_count,
        header.hidden_size,
        header.output_bucket_count,
        header.quantization_a,
        header.quantization_b,
        header.eval_scale,
        header.activation,
        header.parameter_count,
        header.checksum,
    )


def dumps(parameters: NetworkParameters) -> bytes:
    """Serializes parameters into a complete network file."""
    payload = pack_payload(parameters)
    header = Header.for_current_architecture(checksum(payload))
    return pack_header(header) + payload


def write(path: str | Path, parameters: NetworkParameters) -> Path:
    # Atomic: a run interrupted mid-write would otherwise leave a truncated
    # network where the previous good one was.
    return atomic_write_bytes(path, dumps(parameters))


def read_header(data: bytes) -> Header:
    if len(data) < HEADER_BYTES:
        raise NetworkFormatError("file is smaller than its header")

    fields = _HEADER_STRUCT.unpack(data[:HEADER_BYTES])
    if fields[0] != MAGIC:
        raise NetworkFormatError("file is not a NeraChess network")

    header = Header(*fields[1:])
    if header.version != VERSION:
        raise NetworkFormatError(
            f"unsupported format version {header.version}, expected {VERSION}"
        )
    if not header.matches_current_architecture():
        raise NetworkFormatError(
            "network was built for a different architecture than "
            f"{arch.describe()}"
        )
    return header


def loads(data: bytes) -> tuple[Header, NetworkParameters]:
    header = read_header(data)

    payload_bytes = header.parameter_count * arch.PARAMETER_BYTES
    payload = data[HEADER_BYTES : HEADER_BYTES + payload_bytes]
    if len(payload) != payload_bytes:
        raise NetworkFormatError("file is truncated")
    if checksum(payload) != header.checksum:
        raise NetworkFormatError("file failed its checksum")

    values = list(struct.unpack(f"<{header.parameter_count}h", payload))
    cursor = 0

    def take(count: int) -> list[int]:
        nonlocal cursor
        chunk = values[cursor : cursor + count]
        cursor += count
        return chunk

    parameters = NetworkParameters(
        feature_weights=take(arch.FEATURE_WEIGHT_COUNT),
        feature_bias=take(arch.FEATURE_BIAS_COUNT),
        output_weights=take(arch.OUTPUT_WEIGHT_COUNT),
        output_bias=take(arch.OUTPUT_BIAS_COUNT),
    )
    return header, parameters


def read(path: str | Path) -> tuple[Header, NetworkParameters]:
    return loads(Path(path).read_bytes())
