"""Training samples and how they reach the model.

Sample format (plain text, one per line, ``#`` comments allowed)::

    <fen> | <score> | <result>

``score``   Engine evaluation in centipawns from the side-to-move point of view.
``result``  Game result from the side-to-move point of view: 1.0 win,
            0.5 draw, 0.0 loss.

Text is the interchange format because it is trivial to inspect, diff, and
concatenate across machines. Parsing it is slow enough to matter at a few
million positions, so :class:`FeatureCache` extracts the features once and can
persist them in a packed binary form.
"""

from __future__ import annotations

import random
import struct
import sys
import warnings
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
    """Streams samples from a text file without holding the file in memory.

    A malformed line is fatal, with one exception: the *last* line of a file may
    be a torn write. That is what a generator killed mid-flush leaves behind,
    and refusing to read millions of good positions because of it helps nobody.
    It is warned about and skipped. A malformed line anywhere else still raises,
    because that means the data is corrupt rather than merely truncated.

    Parsing is deferred by one line so the reader can tell which line is last.
    """
    source = str(path)
    with Path(path).open("r", encoding="utf-8") as handle:
        held: tuple[str, int] | None = None

        for line_number, line in enumerate(handle, start=1):
            if held is not None:
                sample = parse_line(held[0], held[1], source)
                if sample is not None:
                    yield sample
            held = (line, line_number)

        if held is not None:
            try:
                sample = parse_line(held[0], held[1], source)
            except ValueError as error:
                warnings.warn(
                    f"ignoring a truncated final line in {source}: {error}. "
                    "This is what an interrupted generator leaves behind; the "
                    "file is short by however much that run had left to write.",
                    stacklevel=2,
                )
                return
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
        self,
        batch_size: int,
        shuffle_seed: int | None = None,
        indices: Sequence[int] | None = None,
    ) -> Iterator[Batch]:
        """Yields batches, optionally shuffled and optionally over a subset.

        Shuffling matters more than usual here: self-play samples arrive in
        game order, so an unshuffled batch is a few positions from the same
        handful of games and its gradient is badly correlated.

        `indices` restricts iteration to a subset, which is how a training and
        a validation split share one cache without copying it.
        """
        if batch_size < 1:
            raise ValueError("batch size must be positive")

        order = list(range(len(self))) if indices is None else list(indices)
        if shuffle_seed is not None:
            random.Random(shuffle_seed).shuffle(order)

        for start in range(0, len(order), batch_size):
            yield self.batch(order[start : start + batch_size])

    def split(self, validation_size: int, seed: int) -> tuple[list[int], list[int]]:
        """Deterministically splits into (training, validation) index lists.

        Shuffled before splitting, because samples arrive in game order and a
        contiguous tail would be a handful of whole games rather than a sample
        of the distribution.
        """
        order = list(range(len(self)))
        random.Random(seed).shuffle(order)
        validation_size = max(0, min(validation_size, len(order)))
        return order[validation_size:], order[:validation_size]


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

# -- reading a pack without loading it -------------------------------------

#: Samples read contiguously before the next seek. Random access across a
#: thirty-gigabyte file would seek per sample and spend the epoch waiting on
#: the disk; reading in blocks keeps the access pattern close to sequential.
SHUFFLE_BLOCK = 1 << 16

#: Blocks held together and shuffled as one. Sixteen blocks is about a million
#: samples, which decorrelates a batch just as well as a global shuffle for
#: data that is already in no particular order, at a fraction of the seeking.
SHUFFLE_WINDOW = 16


class _AllExcept:
    """Every index below ``total`` except a small held-out set.

    Materialising the training indices as a list costs 1.4GB at three hundred
    million positions, which would undo the point of not loading the samples
    themselves. Only iteration and length are ever asked of it.
    """

    def __init__(self, total: int, excluded: Iterable[int]):
        self._total = total
        self._excluded = frozenset(excluded)

    def __len__(self) -> int:
        return self._total - len(self._excluded)

    def __bool__(self) -> bool:
        return len(self) > 0

    def __iter__(self) -> Iterator[int]:
        excluded = self._excluded
        for index in range(self._total):
            if index not in excluded:
                yield index

    @property
    def total(self) -> int:
        return self._total

    @property
    def excluded(self) -> frozenset:
        return self._excluded


class MemmapFeatureCache:
    """A packed dataset read off disk instead of into memory.

    :meth:`FeatureCache.load` allocates every array up front. That is fine at
    twenty-three million positions -- 2.7GB -- and impossible at three hundred
    and forty million, which is 30GB on a machine with seven and a Lichess bot
    that must not be disturbed. The packed layout is a header followed by
    contiguous fixed-width arrays, so each one is mapped where it lies and the
    operating system pages in only what a batch touches.

    Presents the same interface as :class:`FeatureCache`, so the trainer does
    not know which one it has.
    """

    _DTYPES = {"H": "<u2", "I": "<u4", "f": "<f4", "B": "<u1"}

    def __init__(self, path: str | Path):
        import numpy as np

        self._path = Path(path)
        with self._path.open("rb") as handle:
            if handle.read(len(PACK_MAGIC)) != PACK_MAGIC:
                raise ValueError(f"{path} is not a packed NeraChess dataset")
            (version,) = struct.unpack("<I", handle.read(4))
            if version != PACK_VERSION:
                raise ValueError(
                    f"{path} uses pack version {version}, expected {PACK_VERSION}"
                )
            (self._length,) = struct.unpack("<Q", handle.read(8))
            counts = [
                struct.unpack("<Q", handle.read(8))[0]
                for _ in FeatureCache._ARRAYS
            ]
            offset = handle.tell()

        expected = self._path.stat().st_size
        self._arrays = {}
        for (name, typecode), count in zip(FeatureCache._ARRAYS, counts):
            dtype = np.dtype(self._DTYPES[typecode])
            end = offset + count * dtype.itemsize
            if end > expected:
                raise ValueError(f"{path} is truncated inside {name}")
            self._arrays[name] = np.memmap(
                self._path, dtype=dtype, mode="r", offset=offset, shape=(count,)
            )
            offset = end
        if offset != expected:
            raise ValueError(
                f"{path} has {expected - offset} trailing bytes; the header and "
                "the file disagree about its contents"
            )

        for name in self._arrays:
            setattr(self, name, self._arrays[name])

    def __len__(self) -> int:
        return self._length

    def split(self, validation_size: int, seed: int):
        """Held-out indices, and everything else as a lazy view."""
        validation_size = max(0, min(validation_size, self._length))
        rng = random.Random(seed)
        held = array("i", sorted(rng.sample(range(self._length), validation_size)))
        return _AllExcept(self._length, held), held

    def batch(self, order: Sequence[int]) -> "Batch":
        import numpy as np

        order = np.asarray(order, dtype=np.int64)
        pieces = []
        offsets = []
        for name, offname in (("own_indices", "own_offsets"),
                              ("their_indices", "their_offsets")):
            bounds = self._arrays[offname]
            starts = bounds[order].astype(np.int64)
            ends = bounds[order + 1].astype(np.int64)
            lengths = ends - starts
            source = self._arrays[name]
            flat = np.concatenate(
                [source[s:e] for s, e in zip(starts, ends)]
            ) if len(order) else np.empty(0, dtype=source.dtype)
            starts_out = np.zeros(len(order), dtype=np.int64)
            if len(order):
                starts_out[1:] = np.cumsum(lengths)[:-1]
            pieces.append(flat)
            offsets.append(starts_out)

        extra = {}
        if "output_buckets" in self._arrays:
            extra["output_buckets"] = self._arrays["output_buckets"][order].tolist()

        return Batch(
            own_indices=pieces[0].tolist(),
            own_offsets=offsets[0].tolist(),
            their_indices=pieces[1].tolist(),
            their_offsets=offsets[1].tolist(),
            scores=self._arrays["scores"][order].tolist(),
            results=self._arrays["results"][order].tolist(),
            **extra,
        )

    def batches(
        self,
        batch_size: int,
        shuffle_seed: int | None = None,
        indices: Sequence[int] | None = None,
    ) -> Iterator["Batch"]:
        if batch_size < 1:
            raise ValueError("batch size must be positive")
        if indices is None:
            indices = _AllExcept(self._length, ())

        pending: list[int] = []

        def drain(final: bool):
            while len(pending) >= batch_size:
                yield self.batch(pending[:batch_size])
                del pending[:batch_size]
            if final and pending:
                yield self.batch(list(pending))
                pending.clear()

        if shuffle_seed is None:
            for index in indices:
                pending.append(index)
                if len(pending) >= batch_size:
                    yield from drain(False)
            yield from drain(True)
            return

        rng = random.Random(shuffle_seed)
        window: list[int] = []
        for block in self._blocks(indices, rng):
            window.extend(block)
            if len(window) >= SHUFFLE_BLOCK * SHUFFLE_WINDOW:
                rng.shuffle(window)
                pending.extend(window)
                window.clear()
                yield from drain(False)
        if window:
            rng.shuffle(window)
            pending.extend(window)
        yield from drain(True)

    def _blocks(self, indices, rng) -> Iterator[list[int]]:
        """Contiguous runs of indices, in shuffled order.

        Built one at a time: holding them all would be the index list this
        class exists to avoid.
        """
        if isinstance(indices, _AllExcept):
            starts = list(range(0, indices.total, SHUFFLE_BLOCK))
            rng.shuffle(starts)
            excluded = indices.excluded
            for start in starts:
                stop = min(start + SHUFFLE_BLOCK, indices.total)
                yield [i for i in range(start, stop) if i not in excluded]
        else:
            materialised = list(indices)
            starts = list(range(0, len(materialised), SHUFFLE_BLOCK))
            rng.shuffle(starts)
            for start in starts:
                yield materialised[start:start + SHUFFLE_BLOCK]


#: Packs larger than this are mapped rather than read into memory.
MEMMAP_THRESHOLD_BYTES = 3 * 1024 ** 3


def open_pack(path: str | Path, memmap: bool | None = None):
    """Open a packed dataset, mapping it when loading it would not fit.

    The choice is reported rather than silent, because it changes the memory
    the run needs by an order of magnitude and that is worth seeing in a log.
    """
    path = Path(path)
    size = path.stat().st_size
    if memmap is None:
        memmap = size > MEMMAP_THRESHOLD_BYTES
    if memmap:
        print(f"mapping {path.name} ({size / 1e9:.2f} GB) from disk", flush=True)
        return MemmapFeatureCache(path)
    print(f"loading {path.name} ({size / 1e9:.2f} GB) into memory", flush=True)
    return FeatureCache.load(path)
