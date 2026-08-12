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

        if info.best_move in ("0000", "(none)", ""):
            break
        try:
            move = chess.Move.from_uci(info.best_move)
        except ValueError:
            break
        if move not in board.legal_moves:
            # An engine that returns an illegal move forfeits; silently scoring
            # it a draw would hide a real bug.
            raise SystemExit(
                f"illegal move {info.best_move} in position {board.fen()}"
            )
        board.push(move)
        moves.append(info.best_move)

    outcome = board.outcome(claim_draw=True)
    if outcome is None or outcome.winner is None:
        return 0.5
    return 1.0 if outcome.winner == chess.WHITE else 0.0


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

    pairs = max(1, arguments.games // 2)
    rng = random.Random(arguments.seed)

    # A wins, B wins, draws -- counted from A's point of view.
    wins = losses = draws = 0

    with UciEngine(arguments.engine, eval_file=arguments.a) as engine_a, \
         UciEngine(arguments.engine, eval_file=arguments.b) as engine_b:
        for pair in range(pairs):
            opening = random_opening(rng, arguments.opening_plies)

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

                if a_score == 1.0:
                    wins += 1
                elif a_score == 0.0:
                    losses += 1
                else:
                    draws += 1

            played = wins + losses + draws
            score = (wins + 0.5 * draws) / played
            print(
                f"  after {played:3d} games: "
                f"+{wins} -{losses} ={draws}  ({score * 100:.1f}%)",
                flush=True,
            )

    played = wins + losses + draws
    score = (wins + 0.5 * draws) / played

    # Normal approximation on the per-game score. Draws carry no variance of
    # their own, so this is the usual sqrt(p(1-p)/n) on the win/loss split.
    variance = (wins * (1 - score) ** 2 + losses * score ** 2 + draws * (0.5 - score) ** 2)
    stderr = math.sqrt(variance / played) / math.sqrt(played) if played else 0.0
    low, high = score - 1.96 * stderr, score + 1.96 * stderr

    print(f"\n{arguments.a.name} vs {arguments.b.name}")
    print(f"  +{wins} -{losses} ={draws} out of {played}")
    print(f"  score {score * 100:.1f}%  (95% CI {low * 100:.1f}% .. {high * 100:.1f}%)")
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
