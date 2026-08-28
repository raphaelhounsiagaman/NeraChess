#!/usr/bin/env python3

"""Tests for the strength test's stopping rule.

These decide whether a change is merged, so the arithmetic behind them is worth
pinning down. Run with:

    python3 -m unittest discover -s scripts/strength/tests -t .
"""

import importlib.util
import math
import sys
import unittest
from pathlib import Path


def load(name: str):
    path = Path(__file__).resolve().parents[1] / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


summarize = load("Summarize-Results")
planner = load("Plan-Stages")


class EloConversion(unittest.TestCase):
    def test_even_score_is_zero_elo(self):
        self.assertAlmostEqual(summarize.logistic_elo(0.5), 0.0)
        self.assertAlmostEqual(summarize.logistic_score(0.0), 0.5)

    def test_conversions_are_inverses(self):
        for elo in (-400.0, -5.0, 0.0, 5.0, 65.0, 400.0):
            round_trip = summarize.logistic_elo(summarize.logistic_score(elo))
            self.assertAlmostEqual(round_trip, elo, places=9)


class PairStatistics(unittest.TestCase):
    def test_reproduces_a_recorded_result(self):
        # The AVX2 accumulator test on this repository: 1,000 games scoring
        # 51.80%, reported as +12.5 Elo with an interval of [-0.3, +25.3].
        penta = [14, 100, 239, 130, 17]
        pairs, mean, variance = summarize.pair_statistics(penta)
        self.assertEqual(pairs, 500)
        self.assertAlmostEqual(mean, 0.518)

        elo = summarize.logistic_elo(mean)
        standard_error = math.sqrt(variance / pairs)
        low = summarize.logistic_elo(mean - 1.96 * standard_error)
        high = summarize.logistic_elo(mean + 1.96 * standard_error)
        self.assertAlmostEqual(elo, 12.5, places=1)
        self.assertAlmostEqual(low, -0.3, places=1)
        self.assertAlmostEqual(high, 25.3, places=1)

    def test_a_pair_score_matches_the_game_score(self):
        # A symmetric spread of pairs is an even score however it is spread.
        pairs, mean, _ = summarize.pair_statistics([1, 1, 0, 1, 1])
        self.assertEqual(pairs, 4)
        self.assertAlmostEqual(mean, 0.5)

    def test_no_pairs_is_not_an_error(self):
        self.assertEqual(summarize.pair_statistics([0, 0, 0, 0, 0]), (0, 0.0, 0.0))


class LogLikelihoodRatio(unittest.TestCase):
    def elo_bounds(self, alpha=0.05, beta=0.05):
        return math.log(beta / (1.0 - alpha)), math.log((1.0 - beta) / alpha)

    def test_an_even_result_moves_towards_rejection(self):
        penta = [10, 100, 280, 100, 10]
        pairs, mean, variance = summarize.pair_statistics(penta)
        self.assertAlmostEqual(mean, 0.5)
        llr = summarize.log_likelihood_ratio(pairs, mean, variance, 0.0, 5.0)
        self.assertLess(llr, 0.0)

    def test_a_large_gain_accepts_the_alternative(self):
        # A lopsided result should clear the upper bound well inside one stage.
        penta = [0, 10, 100, 240, 150]
        pairs, mean, variance = summarize.pair_statistics(penta)
        llr = summarize.log_likelihood_ratio(pairs, mean, variance, 0.0, 5.0)
        _, upper = self.elo_bounds()
        self.assertGreater(llr, upper)

    def test_the_ratio_grows_with_the_number_of_pairs(self):
        single = [14, 100, 239, 130, 17]
        doubled = [2 * count for count in single]
        one = summarize.log_likelihood_ratio(*summarize.pair_statistics(single), 0.0, 5.0)
        two = summarize.log_likelihood_ratio(*summarize.pair_statistics(doubled), 0.0, 5.0)
        self.assertGreater(two, 1.9 * one)

    def test_too_few_pairs_decide_nothing(self):
        self.assertEqual(summarize.log_likelihood_ratio(1, 1.0, 0.0, 0.0, 5.0), 0.0)

    def test_an_identical_engine_never_divides_by_zero(self):
        # Every pair drawn: no variance at all.
        pairs, mean, variance = summarize.pair_statistics([0, 0, 500, 0, 0])
        self.assertEqual(variance, 0.0)
        self.assertEqual(
            summarize.log_likelihood_ratio(pairs, mean, variance, 0.0, 5.0), 0.0
        )


class StagePlanning(unittest.TestCase):
    def test_stages_sum_to_the_requested_total(self):
        for games in (1000, 2000, 4000, 12000, planner.MAX_TOTAL_GAMES):
            self.assertEqual(sum(planner.plan(games)), games)

    def test_a_short_test_uses_one_stage(self):
        self.assertEqual(planner.plan(1000), [1000])

    def test_every_stage_divides_into_whole_pairs_per_shard(self):
        for stage_games in planner.plan(planner.MAX_TOTAL_GAMES):
            self.assertEqual(stage_games % planner.GAMES_PER_STAGE_UNIT, 0)

    def test_stages_grow_apart_from_a_truncated_last_one(self):
        for games in (4000, 12000, planner.MAX_TOTAL_GAMES):
            leading = planner.plan(games)[:-1]
            self.assertEqual(leading, sorted(leading))

    def test_the_first_stage_is_the_length_of_a_screening_test(self):
        # A change large enough to see quickly should not have to wait for more
        # than the thousand games the fixed-length test used to play.
        self.assertEqual(planner.plan(planner.MAX_TOTAL_GAMES)[0], 1000)

    def test_rounding_reaches_whole_pairs_per_shard(self):
        self.assertEqual(planner.round_to_stage_unit(1), 8)
        self.assertEqual(planner.round_to_stage_unit(1000), 1000)
        self.assertEqual(planner.round_to_stage_unit(1001), 1008)

    def test_an_impossible_request_is_refused(self):
        with self.assertRaises(ValueError):
            planner.plan(0)
        with self.assertRaises(ValueError):
            planner.plan(planner.MAX_TOTAL_GAMES + 8)


if __name__ == "__main__":
    unittest.main()
