"""Checks sample parsing and the batching layout the model consumes."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from nnue_training import dataset as data
from nnue_training import features as feat

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
ENDGAME = "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1"


class ParsingTest(unittest.TestCase):
    def test_parses_a_well_formed_line(self) -> None:
        sample = data.parse_line(f"{START} | 25 | 0.5", 1, "test")
        assert sample is not None
        self.assertEqual(sample.fen, START)
        self.assertEqual(sample.score, 25.0)
        self.assertEqual(sample.result, 0.5)

    def test_skips_blank_lines_and_comments(self) -> None:
        self.assertIsNone(data.parse_line("", 1, "test"))
        self.assertIsNone(data.parse_line("   ", 1, "test"))
        self.assertIsNone(data.parse_line("# a comment", 1, "test"))

    def test_rejects_a_missing_field(self) -> None:
        with self.assertRaises(ValueError):
            data.parse_line(f"{START} | 25", 1, "test")

    def test_rejects_a_result_outside_the_unit_interval(self) -> None:
        with self.assertRaises(ValueError):
            data.parse_line(f"{START} | 25 | 1.5", 1, "test")

    def test_tolerates_a_truncated_final_line(self) -> None:
        # A generator killed mid-flush leaves a torn last line. Losing millions
        # of good positions to it would be absurd, so it warns and skips.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "truncated.txt"
            path.write_text(
                f"{START} | 25 | 1.0\n{ENDGAME} | -40 | 0.0\n8/2p5/3p4/KP5r/1R3p",
                encoding="utf-8",
            )
            with self.assertWarns(UserWarning):
                samples = data.load_text_samples(path)

        self.assertEqual(len(samples), 2)
        self.assertEqual(samples[0].score, 25.0)

    def test_a_malformed_line_in_the_middle_is_still_fatal(self) -> None:
        # Corruption anywhere but the end means the data is wrong, not merely
        # short, and silently training on it would be worse than failing.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corrupt.txt"
            path.write_text(
                f"{START} | 25 | 1.0\ngarbage without separators\n{ENDGAME} | -40 | 0.0\n",
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                data.load_text_samples(path)

    def test_a_complete_final_line_is_kept(self) -> None:
        # The deferred-by-one parsing must not drop the last sample of a file
        # that ends properly, with or without a trailing newline.
        for ending in ("\n", ""):
            with self.subTest(trailing_newline=bool(ending)):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "complete.txt"
                    path.write_text(
                        f"{START} | 25 | 1.0\n{ENDGAME} | -40 | 0.0{ending}",
                        encoding="utf-8",
                    )
                    samples = data.load_text_samples(path)
                self.assertEqual(len(samples), 2)
                self.assertEqual(samples[-1].score, -40.0)

    def test_reads_a_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.txt"
            path.write_text(
                f"# header\n{START} | 25 | 1.0\n\n{ENDGAME} | -40 | 0.0\n",
                encoding="utf-8",
            )
            samples = data.load_text_samples(path)

        self.assertEqual(len(samples), 2)
        self.assertEqual(samples[1].score, -40.0)


class BatchingTest(unittest.TestCase):
    def test_offsets_point_at_each_sample(self) -> None:
        samples = [
            data.Sample(START, 25.0, 1.0),
            data.Sample(ENDGAME, -40.0, 0.0),
        ]
        batch = data.collate(samples)

        self.assertEqual(len(batch), 2)
        self.assertEqual(batch.own_offsets, [0, 32])
        self.assertEqual(len(batch.own_indices), 35)  # 32 pieces plus 3
        self.assertEqual(batch.own_indices[:32], feat.active_features(
            START, feat.Perspective.WHITE
        ))
        self.assertEqual(batch.own_indices[32:], feat.active_features(
            ENDGAME, feat.Perspective.WHITE
        ))

    def test_side_to_move_leads_each_half(self) -> None:
        black_to_move = "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1"
        batch = data.collate([data.Sample(black_to_move, 0.0, 0.5)])
        self.assertEqual(
            batch.own_indices, feat.active_features(black_to_move, feat.Perspective.BLACK)
        )
        self.assertEqual(
            batch.their_indices,
            feat.active_features(black_to_move, feat.Perspective.WHITE),
        )

    def test_batches_keep_the_remainder(self) -> None:
        samples = [data.Sample(START, 0.0, 0.5) for _ in range(5)]
        batches = list(data.batched(samples, batch_size=2))
        self.assertEqual([len(batch) for batch in batches], [2, 2, 1])

    def test_rejects_a_zero_batch_size(self) -> None:
        with self.assertRaises(ValueError):
            list(data.batched([], batch_size=0))


class FeatureCacheTest(unittest.TestCase):
    def samples(self) -> list[data.Sample]:
        return [
            data.Sample(START, 25.0, 1.0),
            data.Sample(ENDGAME, -40.0, 0.0),
            data.Sample("4k3/8/8/3p4/8/8/8/4K3 b - - 0 1", 12.5, 0.5),
        ]

    def test_holds_every_sample(self) -> None:
        cache = data.FeatureCache.from_samples(self.samples())
        self.assertEqual(len(cache), 3)

    def test_batch_matches_collate(self) -> None:
        # The cache is an optimization, so it must produce exactly what the
        # straightforward path produces.
        samples = self.samples()
        cache = data.FeatureCache.from_samples(samples)

        cached = cache.batch(range(len(samples)))
        direct = data.collate(samples)

        self.assertEqual(cached.own_indices, direct.own_indices)
        self.assertEqual(cached.own_offsets, direct.own_offsets)
        self.assertEqual(cached.their_indices, direct.their_indices)
        self.assertEqual(cached.their_offsets, direct.their_offsets)
        self.assertEqual(cached.scores, direct.scores)
        self.assertEqual(cached.results, direct.results)

    def test_batches_cover_every_sample_once(self) -> None:
        samples = [data.Sample(START, float(index), 0.5) for index in range(10)]
        cache = data.FeatureCache.from_samples(samples)

        seen = [score for batch in cache.batches(3) for score in batch.scores]
        self.assertEqual(sorted(seen), [float(index) for index in range(10)])

    def test_shuffling_reorders_without_losing_samples(self) -> None:
        samples = [data.Sample(START, float(index), 0.5) for index in range(50)]
        cache = data.FeatureCache.from_samples(samples)

        ordered = [s for b in cache.batches(10) for s in b.scores]
        shuffled = [s for b in cache.batches(10, shuffle_seed=7) for s in b.scores]

        self.assertNotEqual(ordered, shuffled)
        self.assertEqual(sorted(shuffled), sorted(ordered))

    def test_pack_round_trip(self) -> None:
        cache = data.FeatureCache.from_samples(self.samples())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "samples.pack"
            cache.save(path)
            restored = data.FeatureCache.load(path)

        self.assertEqual(len(restored), len(cache))
        self.assertEqual(list(restored.own_indices), list(cache.own_indices))
        self.assertEqual(list(restored.own_offsets), list(cache.own_offsets))
        self.assertEqual(list(restored.their_indices), list(cache.their_indices))
        self.assertEqual(list(restored.their_offsets), list(cache.their_offsets))
        self.assertEqual(list(restored.scores), list(cache.scores))
        self.assertEqual(list(restored.results), list(cache.results))

    def test_rejects_a_foreign_pack_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bogus.pack"
            path.write_bytes(b"NOTAPACK" + bytes(64))
            with self.assertRaises(ValueError):
                data.FeatureCache.load(path)

    def test_reads_a_text_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.txt"
            path.write_text(
                f"# header\n{START} | 25 | 1.0\n{ENDGAME} | -40 | 0.0\n", encoding="utf-8"
            )
            cache = data.FeatureCache.from_text(path)
        self.assertEqual(len(cache), 2)
        self.assertEqual(list(cache.scores), [25.0, -40.0])


if __name__ == "__main__":
    unittest.main()
