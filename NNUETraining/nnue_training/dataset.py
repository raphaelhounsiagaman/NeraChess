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


class PackedSampleReader:
    """Reader for a fixed-size binary sample format.

    TODO(nnue): define and implement the packed layout. The shape it needs is
    a bitboard-style position encoding plus an int16 score and an int8 result,
    which is about 32 bytes per sample against roughly 90 for text, and it
    removes FEN parsing from the training loop. Until then, use the text
    format; it is fast enough for the first few million positions.
    """

    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)

    def __iter__(self) -> Iterator[Sample]:
        raise NotImplementedError(
            "the packed sample format is not implemented yet; use the text format"
        )


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
