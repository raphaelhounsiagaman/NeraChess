"""Checks that the Python feature indexer matches the engine's.

``feature_vectors.json`` is produced by the C++ side::

    ./bin/Release/NeraChessTests/NeraChessTests --nnue-feature-vectors \\
        > NNUETraining/tests/feature_vectors.json

If this test fails, the trainer and the engine disagree about what the network
input means, and any network trained here would evaluate nonsense in the
engine. Fix the mismatch; do not regenerate the fixture to make it pass unless
the feature set genuinely changed on both sides.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from nnue_training import architecture as arch
from nnue_training import features as feat

FIXTURE = Path(__file__).parent / "feature_vectors.json"


class FeatureVectorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def test_fixture_matches_this_architecture(self) -> None:
        self.assertEqual(
            self.fixture["architectureHash"],
            arch.architecture_hash(),
            "the fixture was generated for a different architecture; rebuild "
            "NeraChessTests and regenerate it",
        )
        self.assertEqual(
            self.fixture["perspectiveInputSize"], arch.PERSPECTIVE_INPUT_SIZE
        )

    def test_indices_match_the_engine(self) -> None:
        perspectives = {
            "white": feat.Perspective.WHITE,
            "black": feat.Perspective.BLACK,
        }
        self.assertTrue(self.fixture["positions"], "fixture has no positions")

        for entry in self.fixture["positions"]:
            fen = entry["fen"]
            perspective = perspectives[entry["perspective"]]
            with self.subTest(fen=fen, perspective=entry["perspective"]):
                self.assertEqual(
                    feat.active_features(fen, perspective),
                    entry["features"],
                )


class FeatureIndexingTest(unittest.TestCase):
    def test_indices_stay_inside_the_input_space(self) -> None:
        for piece in range(12):
            for square in range(64):
                for perspective in feat.Perspective:
                    index = feat.feature_index(perspective, piece, square)
                    self.assertGreaterEqual(index, 0)
                    self.assertLess(index, arch.TOTAL_INPUT_SIZE)

    def test_every_feature_is_reachable_exactly_once(self) -> None:
        seen = set()
        for piece in range(12):
            for square in range(64):
                seen.add(feat.feature_index(feat.Perspective.WHITE, piece, square))
        self.assertEqual(len(seen), arch.PERSPECTIVE_INPUT_SIZE)

    def test_black_perspective_flips_the_board(self) -> None:
        # a1 seen by White is the same index as a8 seen by Black.
        white = feat.feature_index(feat.Perspective.WHITE, feat.Piece.WHITE_ROOK, 0)
        black = feat.feature_index(feat.Perspective.BLACK, feat.Piece.BLACK_ROOK, 56)
        self.assertEqual(white, black)

    def test_mirrored_positions_produce_mirrored_features(self) -> None:
        original = "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1"
        mirrored = "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1"
        self.assertEqual(
            feat.active_features(original, feat.Perspective.WHITE),
            feat.active_features(mirrored, feat.Perspective.BLACK),
        )

    def test_both_perspectives_orders_the_side_to_move_first(self) -> None:
        fen = "4k3/8/8/8/3P4/8/8/4K3 b - - 0 1"
        own, their = feat.both_perspectives(fen)
        self.assertEqual(own, feat.active_features(fen, feat.Perspective.BLACK))
        self.assertEqual(their, feat.active_features(fen, feat.Perspective.WHITE))


class FenParsingTest(unittest.TestCase):
    def test_start_position_has_thirty_two_pieces(self) -> None:
        pieces = feat.parse_fen_pieces(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        )
        self.assertEqual(len(pieces), 32)

    def test_squares_use_the_engine_layout(self) -> None:
        pieces = dict(
            (square, piece)
            for piece, square in feat.parse_fen_pieces(
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
            )
        )
        # a1 = 0 holds a white rook, e8 = 60 holds the black king.
        self.assertEqual(pieces[0], feat.Piece.WHITE_ROOK)
        self.assertEqual(pieces[60], feat.Piece.BLACK_KING)

    def test_rejects_an_unknown_piece(self) -> None:
        with self.assertRaises(ValueError):
            feat.parse_fen_pieces("xnbqkbnr/8/8/8/8/8/8/8 w - - 0 1")


if __name__ == "__main__":
    unittest.main()
