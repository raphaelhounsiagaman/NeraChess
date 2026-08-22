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

import math
import random
import struct
import sys
import warnings
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence

from . import features as feat
from .atomic import atomic_write


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

    # float() happily accepts 'nan' and 'inf'. Either one poisons the loss for
    # the whole batch it lands in, and the resulting network is ruined in a way
    # that looks like a training-rate problem rather than a bad sample.
    if not math.isfinite(parsed_score):
        raise ValueError(f"{source}:{line_number}: score {score!r} is not finite")
    if not math.isfinite(parsed_result):
        raise ValueError(f"{source}:{line_number}: result {result!r} is not finite")

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

    # An offset counts active feature indices, not samples, so the ceiling is
    # reached far earlier than the sample count suggests: roughly 134 million
    # positions at 32 pieces each, or about 330 million at a 13-piece average.
    MAX_OFFSET = 2**32 - 1

    def append(self, sample: Sample) -> None:
        own, their = sample.perspectives()

        # Checked before writing anything, so a cache that cannot hold the
        # sample is not left holding half of it. Without this the failure is
        # an OverflowError from array.append, hours into a pack, naming
        # neither the limit nor what to do about it.
        if (len(self.own_indices) + len(own) > self.MAX_OFFSET or
                len(self.their_indices) + len(their) > self.MAX_OFFSET):
            raise ValueError(
                f"pack format v{PACK_VERSION} stores offsets as unsigned "
                f"32-bit, which tops out at {self.MAX_OFFSET} feature indices "
                f"({len(self)} samples packed so far). Split the dataset "
                "across several packs, or widen the offsets to 64-bit in a "
                "new pack version."
            )

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

    # The on-disk layout is fixed-width, but array typecodes are only
    # guaranteed a minimum size. The memmap reader hard-codes <u2/<u4/<f4, so
    # a platform where these differ would write a pack that reader silently
    # misinterprets. Fail at import instead.
    _ITEMSIZES = {"H": 2, "I": 4, "f": 4}
    for _name, _typecode in _ARRAYS:
        if array(_typecode).itemsize != _ITEMSIZES[_typecode]:
            raise RuntimeError(
                f"array('{_typecode}') is "
                f"{array(_typecode).itemsize} bytes on this platform, but the "
                f"pack format requires {_ITEMSIZES[_typecode]}"
            )
    del _name, _typecode

    @classmethod
    def _read_header(cls, handle, path) -> tuple[int, list[int]]:
        """Reads and validates a pack header, shared by both load paths.

        Both readers used to trust the header to different degrees, so a file
        one rejected the other would happily train on. Everything that can be
        checked before touching the payload is checked here, once.
        """

        def exact(size: int, what: str) -> bytes:
            chunk = handle.read(size)
            if len(chunk) != size:
                raise ValueError(
                    f"{path} is truncated: wanted {size} bytes of {what}, "
                    f"got {len(chunk)}"
                )
            return chunk

        if exact(len(PACK_MAGIC), "magic") != PACK_MAGIC:
            raise ValueError(f"{path} is not a packed NeraChess dataset")
        (version,) = struct.unpack("<I", exact(4, "version"))
        if version != PACK_VERSION:
            raise ValueError(
                f"{path} uses pack version {version}, expected {PACK_VERSION}"
            )
        (sample_count,) = struct.unpack("<Q", exact(8, "sample count"))
        counts = [
            struct.unpack("<Q", exact(8, f"{name} length"))[0]
            for name, _ in cls._ARRAYS
        ]

        # The declared sample count is the authority; every array length is a
        # function of it. A pack whose arrays disagree is corrupt, and training
        # on it silently pairs positions with other positions' labels.
        by_name = dict(zip((name for name, _ in cls._ARRAYS), counts))
        for name in ("scores", "results"):
            if by_name[name] != sample_count:
                raise ValueError(
                    f"{path} declares {sample_count} samples but holds "
                    f"{by_name[name]} {name}"
                )
        for name in ("own_offsets", "their_offsets"):
            if by_name[name] != sample_count + 1:
                raise ValueError(
                    f"{path} declares {sample_count} samples, so {name} must "
                    f"hold {sample_count + 1} entries, not {by_name[name]}"
                )
        return sample_count, counts

    @staticmethod
    def _validate_offsets(path, offsets, indices_length: int, name: str) -> None:
        """Offsets must start at zero, never go backwards, and end at the end."""
        if len(offsets) == 0:
            raise ValueError(f"{path}: {name} is empty")
        if offsets[0] != 0:
            raise ValueError(f"{path}: {name} starts at {offsets[0]}, not 0")
        if offsets[-1] != indices_length:
            raise ValueError(
                f"{path}: {name} ends at {offsets[-1]} but its index array "
                f"holds {indices_length} entries"
            )
        for position in range(1, len(offsets)):
            if offsets[position] < offsets[position - 1]:
                raise ValueError(
                    f"{path}: {name} decreases at entry {position} "
                    f"({offsets[position - 1]} -> {offsets[position]})"
                )

    def save(self, path: str | Path) -> Path:
        """Writes the cache so a later run skips parsing entirely.

        Atomic: a pack is hours of work, and an interrupted save that
        truncated the destination would destroy the previous one as well.
        """
        destination = Path(path)
        with atomic_write(destination) as handle:
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
            _, counts = cls._read_header(handle, path)

            for (name, typecode), count in zip(cls._ARRAYS, counts):
                values = array(typecode)
                try:
                    values.fromfile(handle, count)
                except EOFError as error:
                    # array.fromfile leaves the partial read in place, so the
                    # bare EOFError says nothing about which array ran out.
                    raise ValueError(
                        f"{path} is truncated inside {name}: wanted {count} "
                        f"entries, got {len(values)}"
                    ) from error
                if sys.byteorder != "little":
                    values.byteswap()
                setattr(cache, name, values)

            if handle.read(1):
                raise ValueError(
                    f"{path} has trailing bytes; the header and the file "
                    "disagree about its contents"
                )

        for offsets, indices, name in (
            (cache.own_offsets, cache.own_indices, "own_offsets"),
            (cache.their_offsets, cache.their_indices, "their_offsets"),
        ):
            cls._validate_offsets(path, offsets, len(indices), name)
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
    twenty-three million positions -- 2.7GB -- and impossible at ten times
    that, on a machine with seven and a Lichess bot that must not be
    disturbed. The packed layout is a header followed by contiguous
    fixed-width arrays, so each one is mapped where it lies and the operating
    system pages in only what a batch touches.

    Presents the same interface as :class:`FeatureCache`, so the trainer does
    not know which one it has.

    Mapping does not raise the format's own ceiling: offsets are 32-bit, so a
    single pack holds at most 2**32 - 1 feature indices however it is read.
    See :attr:`FeatureCache.MAX_OFFSET`.
    """

    _DTYPES = {"H": "<u2", "I": "<u4", "f": "<f4", "B": "<u1"}

    def __init__(self, path: str | Path):
        import numpy as np

        self._path = Path(path)
        with self._path.open("rb") as handle:
            self._length, counts = FeatureCache._read_header(handle, path)
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

        # Offsets are validated on the mapped arrays rather than read back into
        # memory: numpy checks the whole thing without paging in the indices.
        for offsets_name, indices_name in (
            ("own_offsets", "own_indices"),
            ("their_offsets", "their_indices"),
        ):
            offsets = self._arrays[offsets_name]
            indices_length = len(self._arrays[indices_name])
            if offsets[0] != 0:
                raise ValueError(
                    f"{path}: {offsets_name} starts at {offsets[0]}, not 0"
                )
            if offsets[-1] != indices_length:
                raise ValueError(
                    f"{path}: {offsets_name} ends at {offsets[-1]} but its "
                    f"index array holds {indices_length} entries"
                )
            if np.any(np.diff(offsets.astype(np.int64)) < 0):
                raise ValueError(f"{path}: {offsets_name} is not monotonic")

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

        return Batch(
            own_indices=pieces[0].tolist(),
            own_offsets=offsets[0].tolist(),
            their_indices=pieces[1].tolist(),
            their_offsets=offsets[1].tolist(),
            scores=self._arrays["scores"][order].tolist(),
            results=self._arrays["results"][order].tolist(),
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
