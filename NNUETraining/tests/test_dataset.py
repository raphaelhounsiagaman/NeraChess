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


if __name__ == "__main__":
    unittest.main()
