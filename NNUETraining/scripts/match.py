#!/usr/bin/env python3
"""Plays two networks against each other to decide which is stronger.

Nothing in the training pipeline checks whether a new generation actually
improved. Falling loss means the network fits its data, not that it plays
better, and a generation trained on weak games can be worse than its parent.
This is the check that answers the question.

    python3 scripts/match.py --a runs/first/gen30.nnue --b runs/first/gen31.nnue --games 100

Both sides run the same engine binary with different ``EvalFile`` values, so
the comparison isolates the network. Games are played in colour-reversed pairs
from a shared random opening, which removes most of the variance from opening
choice, and each move gets a fixed node budget so the result does not depend on
machine load.

Needs ``python-chess`` for legal moves and game-over detection:

    .venv/bin/pip install chess
"""

from __future__ import annotations

import argparse
import math
import random
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from nnue_training.engine import UciEngine  # noqa: E402

try:
    import chess
except ImportError as error:  # pragma: no cover
    raise SystemExit(
        "match.py needs python-chess. Install it with:\n"
        "  .venv/bin/pip install chess"
    ) from error


def random_opening(rng: random.Random, plies: int) -> list[str]:
    """A short random legal opening, so every game pair starts somewhere new."""
    board = chess.Board()
    moves: list[str] = []
    for _ in range(plies):
        legal = list(board.legal_moves)
        if not legal or board.is_game_over():
            break
        move = rng.choice(legal)
        board.push(move)
        moves.append(move.uci())
    return moves


def play_game(
    white: UciEngine,
    black: UciEngine,
    opening: list[str],
    nodes: int,
    max_plies: int,
) -> float:
    """Plays one game. Returns White's score: 1.0, 0.5, or 0.0."""
    board = chess.Board()
    for uci in opening:
        board.push_uci(uci)

    moves = list(opening)
    while not board.is_game_over(claim_draw=True) and len(moves) < max_plies:
        engine = white if board.turn == chess.WHITE else black
        info = engine.search(moves=moves, nodes=nodes)

        # A missing, empty, or unparseable best move is a protocol or search
        # failure, not a result. Falling through to the outcome check scored
        # every one of them as a draw, which is how a broken engine measured
        # as an even match.
        if info.best_move in ("0000", "(none)", ""):
            raise SystemExit(
                f"engine returned no move ({info.best_move!r}) in position "
                f"{board.fen()}"
            )
        try:
            move = chess.Move.from_uci(info.best_move)
        except ValueError:
            raise SystemExit(
                f"engine returned unparseable move {info.best_move!r} in "
                f"position {board.fen()}"
            ) from None
        if move not in board.legal_moves:
            # An engine that returns an illegal move forfeits; silently scoring
            # it a draw would hide a real bug.
            raise SystemExit(
                f"illegal move {info.best_move} in position {board.fen()}"
            )
        board.push(move)
        moves.append(info.best_move)

    outcome = board.outcome(claim_draw=True)
    if outcome is None:
        # No terminal position: the game hit --max-plies. That is a genuine
        # adjudicated draw rather than a failure, but it is a different thing
        # from a drawn game and worth being able to tell apart.
        return 0.5
    if outcome.winner is None:
        return 0.5
    return 1.0 if outcome.winner == chess.WHITE else 0.0


def bootstrap_interval(
    pair_scores: Sequence[float],
    seed: int,
    resamples: int = 10000,
    confidence: float = 0.95,
) -> tuple[float, float]:
    """A percentile bootstrap 95% interval over colour-reversed pairs.

    Resampling whole pairs keeps the correlation between the two games that
    share an opening, which is what the paired design exists to exploit. It
    also cannot produce a bound outside [0, 1], unlike the normal
    approximation it replaces.
    """
    if not pair_scores:
        return 0.0, 1.0
    if len(pair_scores) == 1:
        return 0.0, 1.0

    rng = random.Random(seed)
    count = len(pair_scores)
    means = []
    for _ in range(resamples):
        total = 0.0
        for _ in range(count):
            total += pair_scores[rng.randrange(count)]
        means.append(total / count)
    means.sort()

    tail = (1.0 - confidence) / 2.0
    low = means[min(count_index(len(means), tail), len(means) - 1)]
    high = means[min(count_index(len(means), 1.0 - tail), len(means) - 1)]
    return low, high


def count_index(size: int, quantile: float) -> int:
    return max(0, min(size - 1, int(round(quantile * (size - 1)))))


def elo_difference(score: float) -> float:
    if score <= 0.0:
        return float("-inf")
    if score >= 1.0:
        return float("inf")
    return -400.0 * math.log10(1.0 / score - 1.0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--a", type=Path, required=True, help="first network")
    parser.add_argument("--b", type=Path, required=True, help="second network")
    parser.add_argument(
        "--engine", type=Path,
        default=Path(__file__).resolve().parent.parent.parent
        / "bin/Release/NeraChessUCI/NeraChessUCI",
    )
    parser.add_argument("--games", type=int, default=100,
                        help="rounded down to an even number of colour-reversed pairs")
    parser.add_argument("--nodes", type=int, default=20000, help="node budget per move")
    parser.add_argument("--opening-plies", type=int, default=8)
    parser.add_argument("--max-plies", type=int, default=250)
    parser.add_argument("--seed", type=int, default=1)
    arguments = parser.parse_args(argv)

    # max(1, ...) meant --games 0 and --games 1 both played two games, which
    # the help text does not promise and no caller wants.
    if arguments.games < 2:
        parser.error("--games must be at least 2: a pair is one opening played "
                     "with each colour")
    pairs = arguments.games // 2
    rng = random.Random(arguments.seed)

    # A wins, B wins, draws -- counted from A's point of view.
    wins = losses = draws = 0
    # A's score in each colour-reversed pair, kept for the bootstrap below.
    pair_scores: list[float] = []

    with UciEngine(arguments.engine, eval_file=arguments.a) as engine_a, \
         UciEngine(arguments.engine, eval_file=arguments.b) as engine_b:
        for pair in range(pairs):
            opening = random_opening(rng, arguments.opening_plies)

            pair_total = 0.0
            for a_is_white in (True, False):
                engine_a.new_game()
                engine_b.new_game()
                white, black = (
                    (engine_a, engine_b) if a_is_white else (engine_b, engine_a)
                )
                white_score = play_game(
                    white, black, opening, arguments.nodes, arguments.max_plies
                )
                a_score = white_score if a_is_white else 1.0 - white_score

                pair_total += a_score
                if a_score == 1.0:
                    wins += 1
                elif a_score == 0.0:
                    losses += 1
                else:
                    draws += 1

            pair_scores.append(pair_total / 2.0)
            played = wins + losses + draws
            score = (wins + 0.5 * draws) / played
            print(
                f"  after {played:3d} games: "
                f"+{wins} -{losses} ={draws}  ({score * 100:.1f}%)",
                flush=True,
            )

    played = wins + losses + draws
    score = (wins + 0.5 * draws) / played

    # Bootstrap over colour-reversed pairs, not over individual games. The two
    # games in a pair share an opening, so their results are correlated -- that
    # correlation is the entire reason for playing pairs, and treating the
    # games as independent throws away the variance reduction it buys and
    # reports an interval that is too wide.
    low, high = bootstrap_interval(pair_scores, arguments.seed)

    print(f"\n{arguments.a.name} vs {arguments.b.name}")
    print(f"  +{wins} -{losses} ={draws} out of {played} ({len(pair_scores)} pairs)")
    print(f"  score {score * 100:.1f}%  (95% CI {low * 100:.1f}% .. {high * 100:.1f}%, "
          f"pair bootstrap, seed {arguments.seed})")
    print(f"  Elo   {elo_difference(score):+.0f} "
          f"({elo_difference(max(1e-9, low)):+.0f} .. {elo_difference(min(1 - 1e-9, high)):+.0f})")

    if low > 0.5:
        print(f"\n  {arguments.a.name} is stronger.")
    elif high < 0.5:
        print(f"\n  {arguments.b.name} is stronger.")
    else:
        print("\n  Too close to call at this sample size; play more games.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
