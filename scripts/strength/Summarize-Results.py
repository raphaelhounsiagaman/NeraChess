#!/usr/bin/env python3

import argparse
import math
import re
from pathlib import Path


RESULT_PATTERN = re.compile(
    r"Games: (\d+), Wins: (\d+), Losses: (\d+), Draws: (\d+),"
)
PENTANOMIAL_PATTERN = re.compile(
    r"Ptnml\(0-2\): \[(\d+), (\d+), (\d+), (\d+), (\d+)\]"
)


def logistic_elo(score: float) -> float:
    score = min(max(score, 1e-9), 1.0 - 1e-9)
    return 400.0 * math.log10(score / (1.0 - score))


def parse_log(path: Path) -> tuple[int, int, int, list[int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    results = RESULT_PATTERN.findall(text)
    pentanomials = PENTANOMIAL_PATTERN.findall(text)
    if not results or not pentanomials:
        raise ValueError(f"No completed Fastchess result in {path}")

    games, wins, losses, draws = map(int, results[-1])
    penta = list(map(int, pentanomials[-1]))
    if games != wins + losses + draws:
        raise ValueError(f"Inconsistent W/L/D counts in {path}")
    if games != 2 * sum(penta):
        raise ValueError(f"Inconsistent pentanomial counts in {path}")
    if 2 * wins + draws != sum(index * count for index, count in enumerate(penta)):
        raise ValueError(f"W/L/D and pentanomial scores disagree in {path}")
    return wins, losses, draws, penta


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Combine NeraChess Fastchess shard results."
    )
    parser.add_argument("results", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    logs = sorted(args.results.glob("**/fastchess.log"))
    if not logs:
        raise SystemExit(f"No fastchess.log files below {args.results}")

    wins = losses = draws = 0
    penta = [0, 0, 0, 0, 0]
    for log in logs:
        shard_wins, shard_losses, shard_draws, shard_penta = parse_log(log)
        wins += shard_wins
        losses += shard_losses
        draws += shard_draws
        penta = [left + right for left, right in zip(penta, shard_penta)]

    games = wins + losses + draws
    pairs = sum(penta)
    score = (wins + 0.5 * draws) / games
    elo = logistic_elo(score)

    pair_scores = (0.0, 0.5, 1.0, 1.5, 2.0)
    mean_pair_score = sum(
        count * value for count, value in zip(penta, pair_scores)
    ) / pairs
    if pairs > 1:
        pair_variance = sum(
            count * (value - mean_pair_score) ** 2
            for count, value in zip(penta, pair_scores)
        ) / (pairs - 1)
        score_standard_error = math.sqrt(pair_variance / pairs) / 2.0
    else:
        score_standard_error = 0.0

    low_score = max(1e-9, score - 1.96 * score_standard_error)
    high_score = min(1.0 - 1e-9, score + 1.96 * score_standard_error)
    low_elo = logistic_elo(low_score)
    high_elo = logistic_elo(high_score)

    if pairs < 50:
        verdict = "Insufficient games"
    elif low_elo > 0:
        verdict = "Likely improvement"
    elif high_elo < 0:
        verdict = "Likely regression"
    elif elo >= 5:
        verdict = "Promising, but inconclusive"
    elif elo <= -5:
        verdict = "Concerning, but inconclusive"
    else:
        verdict = "No clear difference"

    summary = f"""## NeraChess strength result

**{verdict}**

| Measurement | Result |
|---|---:|
| Games | {games} |
| Candidate wins | {wins} |
| Candidate losses | {losses} |
| Draws | {draws} |
| Candidate score | {100.0 * score:.2f}% |
| Estimated Elo | {elo:+.1f} |
| Approximate paired 95% interval | [{low_elo:+.1f}, {high_elo:+.1f}] |
| Pentanomial | {penta} |
| Completed shards | {len(logs)} |

The interval is an approximate screening result, not proof of a small Elo
change. Use a longer test before accepting a marginal search change.
"""

    print(summary, end="")
    if args.output:
        args.output.write_text(summary, encoding="utf-8")


if __name__ == "__main__":
    main()
