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


#: Squares by name, so the mirroring tests read like the board does.
A1, B1, C1, D1, E1, F1, G1, H1 = range(8)
A8, B8, C8, D8, E8, F8, G8, H8 = range(56, 64)
D4, E4 = 27, 28
A5, H5 = 32, 39


def direct(perspective: feat.Perspective) -> feat.View:
    return feat.View(perspective, feat.Orientation.DIRECT, 0)


def mirrored(perspective: feat.Perspective) -> feat.View:
    return feat.View(perspective, feat.Orientation.MIRRORED, 0)


class FeatureIndexingTest(unittest.TestCase):
    def test_indices_stay_inside_the_input_space(self) -> None:
        for piece in range(12):
            for square in range(64):
                for perspective in feat.Perspective:
                    for orientation in feat.Orientation:
                        view = feat.View(perspective, orientation, 0)
                        index = feat.feature_index(view, piece, square)
                        self.assertGreaterEqual(index, 0)
                        self.assertLess(index, arch.TOTAL_INPUT_SIZE)

    def test_every_feature_is_reachable_exactly_once(self) -> None:
        seen = set()
        for piece in range(12):
            for square in range(64):
                seen.add(
                    feat.feature_index(direct(feat.Perspective.WHITE), piece, square)
                )
        self.assertEqual(len(seen), arch.PERSPECTIVE_INPUT_SIZE)

    def test_black_perspective_flips_the_board(self) -> None:
        # a1 seen by White is the same index as a8 seen by Black.
        white = feat.feature_index(
            direct(feat.Perspective.WHITE), feat.Piece.WHITE_ROOK, A1
        )
        black = feat.feature_index(
            direct(feat.Perspective.BLACK), feat.Piece.BLACK_ROOK, A8
        )
        self.assertEqual(white, black)

    def test_mirrored_positions_produce_mirrored_features(self) -> None:
        original = "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1"
        colour_mirrored = "4k3/8/8/3p4/8/8/8/4K3 b - - 0 1"
        self.assertEqual(
            feat.active_features(original, feat.Perspective.WHITE),
            feat.active_features(colour_mirrored, feat.Perspective.BLACK),
        )


class SquareMirroringTest(unittest.TestCase):
    def test_files_swap_across_the_board(self) -> None:
        for left, right in (
            (A1, H1),
            (B1, G1),
            (C1, F1),
            (D1, E1),
            (A8, H8),
            (D8, E8),
            (D4, E4),
            (A5, H5),
        ):
            self.assertEqual(feat.mirrored_square(left), right)
            self.assertEqual(feat.mirrored_square(right), left)

    def test_reflection_is_its_own_inverse_and_keeps_the_rank(self) -> None:
        for square in range(64):
            reflected = feat.mirrored_square(square)
            self.assertEqual(feat.mirrored_square(reflected), square)
            self.assertEqual(reflected // 8, square // 8)
            self.assertEqual(reflected % 8, 7 - square % 8)


class KingOrientationTest(unittest.TestCase):
    def test_the_boundary_runs_between_d_and_e(self) -> None:
        self.assertEqual(feat.orientation_of_king(D1), feat.Orientation.MIRRORED)
        self.assertEqual(feat.orientation_of_king(E1), feat.Orientation.DIRECT)
        self.assertEqual(feat.orientation_of_king(A1), feat.Orientation.MIRRORED)
        self.assertEqual(feat.orientation_of_king(H1), feat.Orientation.DIRECT)

    def test_every_square_lands_on_the_right_side(self) -> None:
        for square in range(64):
            expected = (
                feat.Orientation.MIRRORED
                if square % 8 <= 3
                else feat.Orientation.DIRECT
            )
            self.assertEqual(feat.orientation_of_king(square), expected)

    def test_canonicalization_puts_the_king_on_the_king_side(self) -> None:
        for square in range(64):
            view = feat.view_of_king(feat.Perspective.WHITE, square)
            self.assertGreaterEqual(feat.oriented_square(view, square) % 8, 4)

    def test_each_perspective_follows_its_own_king(self) -> None:
        direct_orientation = feat.Orientation.DIRECT
        mirrored_orientation = feat.Orientation.MIRRORED
        cases = (
            ("7k/8/8/8/8/8/8/K7 w - - 0 1", mirrored_orientation, direct_orientation),
            ("k7/8/8/8/8/8/8/7K w - - 0 1", direct_orientation, mirrored_orientation),
            ("3k4/8/8/8/8/8/8/3K4 w - - 0 1", mirrored_orientation, mirrored_orientation),
            ("4k3/8/8/8/8/8/8/4K3 w - - 0 1", direct_orientation, direct_orientation),
        )
        for fen, white, black in cases:
            with self.subTest(fen=fen):
                pieces = feat.parse_fen_pieces(fen)
                self.assertEqual(
                    feat.view_of(pieces, feat.Perspective.WHITE).orientation, white
                )
                self.assertEqual(
                    feat.view_of(pieces, feat.Perspective.BLACK).orientation, black
                )

    def test_a_position_without_a_king_reads_direct(self) -> None:
        # Malformed, but the engine's ViewOf makes the same choice and the two
        # must not disagree about anything.
        pieces = feat.parse_fen_pieces("8/8/8/3p4/8/8/8/8 w - - 0 1")
        self.assertIsNone(feat.king_square(pieces, feat.Perspective.WHITE))
        self.assertEqual(
            feat.view_of(pieces, feat.Perspective.WHITE).orientation,
            feat.Orientation.DIRECT,
        )


class HorizontalCanonicalizationTest(unittest.TestCase):
    def test_a_mirrored_view_reads_a1_where_a_direct_one_reads_h1(self) -> None:
        white = feat.Perspective.WHITE
        self.assertNotEqual(
            feat.feature_index(direct(white), feat.Piece.WHITE_ROOK, A1),
            feat.feature_index(mirrored(white), feat.Piece.WHITE_ROOK, A1),
        )
        self.assertEqual(
            feat.feature_index(mirrored(white), feat.Piece.WHITE_ROOK, A1),
            feat.feature_index(direct(white), feat.Piece.WHITE_ROOK, H1),
        )

    def test_black_mirroring_composes_with_the_vertical_flip(self) -> None:
        # Black's own a8 rook under a mirrored view lands where White's h1 rook
        # lands under a direct one.
        self.assertEqual(
            feat.feature_index(
                mirrored(feat.Perspective.BLACK), feat.Piece.BLACK_ROOK, A8
            ),
            feat.feature_index(
                direct(feat.Perspective.WHITE), feat.Piece.WHITE_ROOK, H1
            ),
        )

    def test_reflected_positions_canonicalize_identically(self) -> None:
        # No castling rights and no en passant, so each pair differs in nothing
        # but the file every piece stands on.
        cases = (
            (
                "2k5/1pp5/8/8/8/8/1PP5/2K5 w - - 0 1",
                "5k2/5pp1/8/8/8/8/5PP1/5K2 w - - 0 1",
            ),
            (
                "8/3k4/2p5/8/8/5N2/6PP/6K1 w - - 0 1",
                "8/4k3/5p2/8/8/2N5/PP6/1K6 w - - 0 1",
            ),
            (
                "7k/8/8/3q4/8/8/8/1K6 w - - 0 1",
                "k7/8/8/4q3/8/8/8/6K1 w - - 0 1",
            ),
            (
                "1r2k3/p1pp4/8/8/8/6N1/PP4PP/2K4R b - - 0 1",
                "3k2r1/4pp1p/8/8/8/1N6/PP4PP/R4K2 b - - 0 1",
            ),
        )
        for fen, reflected in cases:
            with self.subTest(fen=fen):
                self.assertNotEqual(
                    sorted(feat.parse_fen_pieces(fen)),
                    sorted(feat.parse_fen_pieces(reflected)),
                    "the position is its own reflection, so it proves nothing",
                )
                for perspective in feat.Perspective:
                    self.assertEqual(
                        feat.active_features(fen, perspective),
                        feat.active_features(reflected, perspective),
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
