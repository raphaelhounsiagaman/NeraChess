#!/usr/bin/env python3

"""Combine Fastchess shard results and apply the sequential stopping rule.

The workflow plays games in stages and calls this script after each one, so the
verdict here decides whether another stage is worth its hour of runner time.
"""

import argparse
import json
import math
import re
from pathlib import Path


RESULT_PATTERN = re.compile(
    r"Games: (\d+), Wins: (\d+), Losses: (\d+), Draws: (\d+),"
)
PENTANOMIAL_PATTERN = re.compile(
    r"Ptnml\(0-2\): \[(\d+), (\d+), (\d+), (\d+), (\d+)\]"
)

# A pair contributes 0 to 2 game points; normalise to [0, 1] so the mean is
# directly comparable with a per-game score.
PAIR_SCORES = (0.0, 0.25, 0.5, 0.75, 1.0)


def logistic_elo(score: float) -> float:
    score = min(max(score, 1e-9), 1.0 - 1e-9)
    return 400.0 * math.log10(score / (1.0 - score))


def logistic_score(elo: float) -> float:
    return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))


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


def pair_statistics(penta: list[int]) -> tuple[int, float, float]:
    """Return the pair count, mean normalised pair score, and its variance."""
    pairs = sum(penta)
    if pairs == 0:
        return 0, 0.0, 0.0

    mean = sum(
        count * value for count, value in zip(penta, PAIR_SCORES)
    ) / pairs
    if pairs > 1:
        variance = sum(
            count * (value - mean) ** 2
            for count, value in zip(penta, PAIR_SCORES)
        ) / (pairs - 1)
    else:
        variance = 0.0
    return pairs, mean, variance


def log_likelihood_ratio(
    pairs: int, mean: float, variance: float, elo0: float, elo1: float
) -> float:
    """Normal-approximation LLR for H0: Elo = elo0 against H1: Elo = elo1.

    Each opening pair is one observation. Treating the pair score as normal with
    the observed variance, the log likelihood ratio has a closed form; this is
    the standard approximation used for pentanomial SPRT in engine testing.
    """
    if pairs < 2 or variance <= 0.0:
        return 0.0

    score0 = logistic_score(elo0)
    score1 = logistic_score(elo1)
    return (
        pairs
        * (score1 - score0)
        * (2.0 * mean - score0 - score1)
        / (2.0 * variance)
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Combine NeraChess Fastchess shard results."
    )
    parser.add_argument("results", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--decision-output",
        type=Path,
        help="Write continue=true/false and the verdict as key=value lines.",
    )
    parser.add_argument(
        "--mode",
        choices=("sequential", "fixed"),
        default="sequential",
        help="sequential stops as soon as the test is decisive; fixed never does.",
    )
    parser.add_argument("--elo0", type=float, default=0.0)
    parser.add_argument("--elo1", type=float, default=5.0)
    parser.add_argument("--alpha", type=float, default=0.05)
    parser.add_argument("--beta", type=float, default=0.05)
    parser.add_argument(
        "--target-games",
        type=int,
        default=0,
        help="Total games planned; the test stops once they have been played.",
    )
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
    pairs, mean_pair_score, pair_variance = pair_statistics(penta)
    score = (wins + 0.5 * draws) / games
    elo = logistic_elo(score)

    if pairs > 1:
        score_standard_error = math.sqrt(pair_variance / pairs)
    else:
        score_standard_error = 0.0

    low_score = max(1e-9, score - 1.96 * score_standard_error)
    high_score = min(1.0 - 1e-9, score + 1.96 * score_standard_error)
    low_elo = logistic_elo(low_score)
    high_elo = logistic_elo(high_score)

    llr = log_likelihood_ratio(
        pairs, mean_pair_score, pair_variance, args.elo0, args.elo1
    )
    upper_bound = math.log((1.0 - args.beta) / args.alpha)
    lower_bound = math.log(args.beta / (1.0 - args.alpha))

    exhausted = args.target_games > 0 and games >= args.target_games

    if args.mode == "fixed":
        accepted = None
        keep_playing = not exhausted
    elif llr >= upper_bound:
        accepted = "H1"
        keep_playing = False
    elif llr <= lower_bound:
        accepted = "H0"
        keep_playing = False
    else:
        accepted = None
        keep_playing = not exhausted

    if pairs < 50:
        verdict = "Insufficient games"
    elif accepted == "H1":
        verdict = "Accepted: improvement"
    elif accepted == "H0":
        verdict = "Rejected: not an improvement"
    elif args.mode == "fixed" or exhausted:
        if low_elo > 0:
            verdict = "Likely improvement"
        elif high_elo < 0:
            verdict = "Likely regression"
        elif elo >= 5:
            verdict = "Promising, but inconclusive"
        elif elo <= -5:
            verdict = "Concerning, but inconclusive"
        else:
            verdict = "No clear difference"
    else:
        verdict = "Undecided, playing on"

    if args.mode == "fixed":
        rule = f"Fixed length, {args.target_games} games, no early stopping"
    else:
        rule = (
            f"SPRT H0: Elo <= {args.elo0:g} against H1: Elo >= {args.elo1:g}, "
            f"alpha = {args.alpha:g}, beta = {args.beta:g}"
        )

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

Stopping rule: {rule}.
Log likelihood ratio {llr:+.2f}, stopping at {lower_bound:+.2f} or \
{upper_bound:+.2f}.

The interval is a descriptive summary of the games played. Because a sequential
test looks at the data repeatedly, only the accept and reject verdicts above
carry the stated error rates; the interval on its own does not.
"""

    print(summary, end="")
    if args.output:
        args.output.write_text(summary, encoding="utf-8")

    if args.decision_output:
        decision = {
            "continue": "true" if keep_playing else "false",
            "verdict": verdict,
            "games": games,
            "llr": f"{llr:.4f}",
            "elo": f"{elo:.2f}",
            "accepted": accepted or "none",
        }
        args.decision_output.write_text(
            "".join(f"{key}={value}\n" for key, value in decision.items()),
            encoding="utf-8",
        )
        print(json.dumps(decision))


if __name__ == "__main__":
    main()
