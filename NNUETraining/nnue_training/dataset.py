"""Training samples and how they reach the model.

Sample format (plain text, one per line, ``#`` comments allowed)::

    <fen> | <score> | <result>

``score``   Engine evaluation in centipawns from the side-to-move point of view.
``result``  Game result from the side-to-move point of view: 1.0 win,
            0.5 draw, 0.0 loss.

Text is the interchange format because it is trivial to inspect and diff. It
is also slow and roughly six times larger than it needs to be, so a packed
binary format is the intended follow-up -- see :class:`PackedSampleReader`.
"""

from __future__ import annotations

import random
import struct
import sys
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence

from . import features as feat


@dataclass(frozen=True)
class Sample:
    """One labelled position."""

    fen: str
    score: float
    result: float

    def perspectives(self) -> tuple[list[int], list[int]]:
        """Active features for ``(side to move, other side)``."""
        return feat.both_perspectives(self.fen)


def parse_line(line: str, line_number: int, source: str) -> Sample | None:
    stripped = line.strip()
    if not stripped or stripped.startswith("#"):
        return None

    parts = [part.strip() for part in stripped.split("|")]
    if len(parts) != 3:
        raise ValueError(
            f"{source}:{line_number}: expected '<fen> | <score> | <result>', "
            f"got {stripped!r}"
        )

    fen, score, result = parts
    try:
        parsed_score = float(score)
        parsed_result = float(result)
    except ValueError as error:
        raise ValueError(f"{source}:{line_number}: {error}") from error

    if not 0.0 <= parsed_result <= 1.0:
        raise ValueError(
            f"{source}:{line_number}: result {parsed_result} is outside [0, 1]"
        )
    return Sample(fen=fen, score=parsed_score, result=parsed_result)


def read_text_samples(path: str | Path) -> Iterator[Sample]:
    """Streams samples from a text file without holding the file in memory."""
    source = str(path)
    with Path(path).open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            sample = parse_line(line, line_number, source)
            if sample is not None:
                yield sample


def load_text_samples(path: str | Path) -> list[Sample]:
    return list(read_text_samples(path))


PACK_MAGIC = b"NERAPACK"
PACK_VERSION = 1


class FeatureCache:
    """Feature indices extracted once, so epochs do not re-parse FENs.

    Parsing a FEN and computing its features costs far more than the gradient
    step that consumes them, and doing it every epoch makes training on a few
    million positions pointlessly slow. This holds the whole dataset as flat
    arrays -- every sample's features concatenated, plus per-sample offsets --
    which is both fast to slice and compact: two bytes per active feature
    rather than a Python int per feature.

    Uses only the standard library's ``array`` module, so a dataset can be
    packed and inspected without PyTorch.
    """

    def __init__(self) -> None:
        self.own_indices = array("H")
        self.own_offsets = array("I", [0])
        self.their_indices = array("H")
        self.their_offsets = array("I", [0])
        self.scores = array("f")
        self.results = array("f")

    def __len__(self) -> int:
        return len(self.scores)

    def append(self, sample: Sample) -> None:
        own, their = sample.perspectives()
        self.own_indices.extend(own)
        self.own_offsets.append(len(self.own_indices))
        self.their_indices.extend(their)
        self.their_offsets.append(len(self.their_indices))
        self.scores.append(sample.score)
        self.results.append(sample.result)

    @classmethod
    def from_samples(cls, samples: Iterable[Sample]) -> "FeatureCache":
        cache = cls()
        for sample in samples:
            cache.append(sample)
        return cache

    @classmethod
    def from_text(cls, path: str | Path) -> "FeatureCache":
        return cls.from_samples(read_text_samples(path))

    # -- persistence ------------------------------------------------------

    _ARRAYS = (
        ("own_indices", "H"),
        ("own_offsets", "I"),
        ("their_indices", "H"),
        ("their_offsets", "I"),
        ("scores", "f"),
        ("results", "f"),
    )

    def save(self, path: str | Path) -> Path:
        """Writes the cache so a later run skips parsing entirely."""
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("wb") as handle:
            handle.write(PACK_MAGIC)
            handle.write(struct.pack("<I", PACK_VERSION))
            handle.write(struct.pack("<Q", len(self)))
            for name, _ in self._ARRAYS:
                values: array = getattr(self, name)
                handle.write(struct.pack("<Q", len(values)))
            for name, _ in self._ARRAYS:
                values = getattr(self, name)
                if sys.byteorder != "little":
                    values = array(values.typecode, values)
                    values.byteswap()
                values.tofile(handle)
        return destination

    @classmethod
    def load(cls, path: str | Path) -> "FeatureCache":
        cache = cls()
        with Path(path).open("rb") as handle:
            if handle.read(len(PACK_MAGIC)) != PACK_MAGIC:
                raise ValueError(f"{path} is not a packed NeraChess dataset")
            (version,) = struct.unpack("<I", handle.read(4))
            if version != PACK_VERSION:
                raise ValueError(
                    f"{path} uses pack version {version}, expected {PACK_VERSION}"
                )
            handle.read(8)  # sample count, implied by the array lengths

            counts = [struct.unpack("<Q", handle.read(8))[0] for _ in cls._ARRAYS]
            for (name, typecode), count in zip(cls._ARRAYS, counts):
                values = array(typecode)
                values.fromfile(handle, count)
                if sys.byteorder != "little":
                    values.byteswap()
                setattr(cache, name, values)
        return cache

    # -- batching ---------------------------------------------------------

    def batch(self, order: Sequence[int]) -> Batch:
        """Collates the samples at the given indices into one batch."""
        own_indices: list[int] = []
        own_offsets: list[int] = []
        their_indices: list[int] = []
        their_offsets: list[int] = []
        scores: list[float] = []
        results: list[float] = []

        for sample_index in order:
            own_offsets.append(len(own_indices))
            own_indices.extend(
                self.own_indices[
                    self.own_offsets[sample_index] : self.own_offsets[sample_index + 1]
                ]
            )
            their_offsets.append(len(their_indices))
            their_indices.extend(
                self.their_indices[
                    self.their_offsets[sample_index] : self.their_offsets[sample_index + 1]
                ]
            )
            scores.append(self.scores[sample_index])
            results.append(self.results[sample_index])

        return Batch(
            own_indices=own_indices,
            own_offsets=own_offsets,
            their_indices=their_indices,
            their_offsets=their_offsets,
            scores=scores,
            results=results,
        )

    def batches(
        self, batch_size: int, shuffle_seed: int | None = None
    ) -> Iterator[Batch]:
        """Yields batches, optionally in a shuffled order.

        Shuffling matters more than usual here: self-play samples arrive in
        game order, so an unshuffled batch is a few positions from the same
        handful of games and its gradient is badly correlated.
        """
        if batch_size < 1:
            raise ValueError("batch size must be positive")

        order = list(range(len(self)))
        if shuffle_seed is not None:
            random.Random(shuffle_seed).shuffle(order)

        for start in range(0, len(order), batch_size):
            yield self.batch(order[start : start + batch_size])


@dataclass
class Batch:
    """A collated batch in the flat-index layout :class:`NnueModel` expects."""

    own_indices: list[int]
    own_offsets: list[int]
    their_indices: list[int]
    their_offsets: list[int]
    scores: list[float]
    results: list[float]

    def __len__(self) -> int:
        return len(self.scores)


def collate(samples: Sequence[Sample]) -> Batch:
    """Flattens a batch's feature lists into indices plus per-sample offsets.

    Deliberately free of PyTorch so it can be unit tested on its own; the
    training loop converts the lists to tensors.
    """
    own_indices: list[int] = []
    own_offsets: list[int] = []
    their_indices: list[int] = []
    their_offsets: list[int] = []

    for sample in samples:
        own, their = sample.perspectives()
        own_offsets.append(len(own_indices))
        own_indices.extend(own)
        their_offsets.append(len(their_indices))
        their_indices.extend(their)

    return Batch(
        own_indices=own_indices,
        own_offsets=own_offsets,
        their_indices=their_indices,
        their_offsets=their_offsets,
        scores=[sample.score for sample in samples],
        results=[sample.result for sample in samples],
    )


def batched(samples: Iterable[Sample], batch_size: int) -> Iterator[Batch]:
    """Groups samples into collated batches, dropping no remainder."""
    if batch_size < 1:
        raise ValueError("batch size must be positive")

    pending: list[Sample] = []
    for sample in samples:
        pending.append(sample)
        if len(pending) == batch_size:
            yield collate(pending)
            pending = []
    if pending:
        yield collate(pending)
