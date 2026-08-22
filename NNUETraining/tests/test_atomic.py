"""Checks that a failed write leaves the previous file intact."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from nnue_training import dataset as data
from nnue_training.atomic import atomic_write, atomic_write_bytes

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


class AtomicWriteTest(unittest.TestCase):
    def test_replaces_the_destination_on_success(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "file.bin"
            atomic_write_bytes(path, b"new")
            self.assertEqual(path.read_bytes(), b"new")

    def test_leaves_the_previous_file_when_the_write_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file.bin"
            path.write_bytes(b"original")

            with self.assertRaises(RuntimeError):
                with atomic_write(path) as handle:
                    handle.write(b"half a file")
                    raise RuntimeError("interrupted")

            self.assertEqual(path.read_bytes(), b"original")

    def test_leaves_no_partial_file_behind(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file.bin"
            with self.assertRaises(RuntimeError):
                with atomic_write(path) as handle:
                    handle.write(b"half a file")
                    raise RuntimeError("interrupted")

            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_a_failed_pack_save_does_not_destroy_the_previous_pack(self) -> None:
        cache = data.FeatureCache.from_samples(
            [data.Sample(fen=START, score=25.0, result=1.0)]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.pack"
            cache.save(path)
            good = path.read_bytes()

            # An array that cannot be written part-way through the save.
            broken = data.FeatureCache.from_samples(
                [data.Sample(fen=START, score=25.0, result=1.0)]
            )
            broken.results = "not an array"  # type: ignore[assignment]
            with self.assertRaises(Exception):
                broken.save(path)

            self.assertEqual(path.read_bytes(), good)
            data.FeatureCache.load(path)


if __name__ == "__main__":
    unittest.main()
