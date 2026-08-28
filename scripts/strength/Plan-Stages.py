#!/usr/bin/env python3

"""Divide a strength test into the stages the workflow plays one after another.

Stages exist so a sequential test can stop early: the workflow plays one, asks
Summarize-Results.py whether the answer is already clear, and only pays for the
next one if it is not. Early stages are small so an obvious change resolves
quickly; later ones are larger so a genuinely small change still accumulates
games at a reasonable rate.
"""

import argparse
from pathlib import Path


SHARDS = 4
GAMES_PER_PAIR = 2
GAMES_PER_STAGE_UNIT = SHARDS * GAMES_PER_PAIR

# Games per stage. A stage of 1,000 games takes about 70 minutes of wall clock;
# the largest here is about 4.5 hours, which keeps every shard job clear of the
# six-hour limit GitHub imposes on a hosted runner.
STAGE_LADDER = (1000, 1000, 2000, 2000, 4000, 4000, 4000)
MAX_TOTAL_GAMES = sum(STAGE_LADDER)


def plan(total_games: int) -> list[int]:
    """Return the games played in each stage, shortest schedule reaching the total."""
    if total_games < GAMES_PER_STAGE_UNIT:
        raise ValueError(
            f"A test needs at least {GAMES_PER_STAGE_UNIT} games, not {total_games}."
        )
    if total_games > MAX_TOTAL_GAMES:
        raise ValueError(
            f"A test cannot exceed {MAX_TOTAL_GAMES} games, and {total_games} "
            "was requested. Run a second test if more are genuinely needed."
        )

    stages = []
    remaining = total_games
    for stage_games in STAGE_LADDER:
        if remaining <= 0:
            break
        stages.append(min(stage_games, remaining))
        remaining -= stages[-1]
    return stages


def round_to_stage_unit(games: int) -> int:
    """Round up so every shard plays a whole number of opening pairs."""
    units = -(-games // GAMES_PER_STAGE_UNIT)
    return units * GAMES_PER_STAGE_UNIT


def main() -> None:
    parser = argparse.ArgumentParser(description="Plan NeraChess strength test stages.")
    parser.add_argument(
        "--games",
        type=int,
        required=True,
        help="Total games to plan for; a sequential test may stop before them.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="File to append key=value lines to, such as GITHUB_OUTPUT.",
    )
    args = parser.parse_args()

    total_games = round_to_stage_unit(args.games)
    try:
        stages = plan(total_games)
    except ValueError as error:
        raise SystemExit(str(error)) from error

    lines = [f"total_games={total_games}", f"stage_count={len(stages)}"]
    for index in range(len(STAGE_LADDER)):
        stage_games = stages[index] if index < len(stages) else 0
        pairs = stage_games // GAMES_PER_STAGE_UNIT
        lines.append(f"pairs_{index + 1}={pairs}")

    report = "\n".join(lines) + "\n"
    print(report, end="")
    if args.output:
        with args.output.open("a", encoding="utf-8") as handle:
            handle.write(report)


if __name__ == "__main__":
    main()
