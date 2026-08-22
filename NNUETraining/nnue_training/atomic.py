"""Writing a file so an interrupted run leaves no half-written result.

A direct write truncates the destination before it has the new contents, so a
run killed part-way through replaces a good dataset or network with a corrupt
one. The next run then loads that file and either fails somewhere far from the
cause or, worse, trains on it.

``NeraChessSelfPlay`` already writes its samples to a partial file and renames
it on completion. This is the same pattern for the Python side, so the whole
pipeline either produces a complete artifact or leaves the previous one alone.
"""

from __future__ import annotations

import os
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


@contextmanager
def atomic_write(path: str | Path, mode: str = "wb") -> Iterator:
    """Yields a handle whose contents replace ``path`` only on clean exit.

    The temporary sits beside the destination rather than in the system
    temporary directory, because a rename across filesystems is not atomic and
    the two are frequently on different devices when the destination is a
    dataset on a mounted volume.
    """
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".partial")

    try:
        with temporary.open(mode) as handle:
            yield handle
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def atomic_write_bytes(path: str | Path, data: bytes) -> Path:
    """Replaces ``path`` with ``data`` in one step."""
    destination = Path(path)
    with atomic_write(destination) as handle:
        handle.write(data)
    return destination
