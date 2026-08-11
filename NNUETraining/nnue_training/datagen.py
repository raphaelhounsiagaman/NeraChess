"""Self-play data generation.

The first network has a bootstrapping problem: this branch removed the
hand-crafted evaluation, so the engine cannot generate its own training data
until it already has a network. The two ways out are:

1. label positions with an existing strong engine, or a public dataset of
   already-labelled positions;
2. label them with a *temporary* material-only evaluation, train a weak first
   network, then re-generate with that network and iterate.

Either way the loop below is the same: walk a game, search each position to a
shallow fixed depth, and write ``fen | score | result`` lines. Positions in
check or where the best move is a capture are skipped, because a static
evaluation is being asked to judge a position the search is still resolving.

TODO(nnue): implement :func:`play_game`. It needs legal move generation and
game-over detection on the Python side, which means either a ``python-chess``
dependency or a new UCI command that reports the game state. Everything around
it -- opening selection, filtering, sharding, the output format -- is here.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Sequence

from .engine import SearchInfo, UciEngine


@dataclass
class DataGenConfig:
    """Settings for one generation run."""

    engine_path: Path
    output_path: Path

    #: Positions to write before stopping.
    target_positions: int = 1_000_000

    #: Fixed search depth per labelled position. Shallow and wide beats deep
    #: and narrow: label quality matters less than position variety.
    depth: int = 8

    #: Random plies played at the start of each game to spread the openings.
    random_opening_plies: int = 8

    #: Skip positions whose absolute score exceeds this. Positions that are
    #: already decided teach the network nothing except to saturate.
    max_absolute_score: int = 2000

    #: Skip positions in check; their evaluation belongs to the search.
    skip_in_check: bool = True

    seed: int = 0

    #: Written to the output header so a dataset can be traced to its run.
    notes: str = ""


@dataclass
class GameRecord:
    """One finished self-play game."""

    #: ``(fen, search info)`` for every position that passed filtering.
    positions: list[tuple[str, SearchInfo]] = field(default_factory=list)

    #: Result from White's point of view: 1.0, 0.5, or 0.0.
    result: float = 0.5


def result_for_side_to_move(result_for_white: float, white_to_move: bool) -> float:
    return result_for_white if white_to_move else 1.0 - result_for_white


def format_sample(fen: str, score: int, result: float) -> str:
    """One dataset line, in the format :mod:`nnue_training.dataset` reads."""
    return f"{fen} | {score} | {result}"


def should_keep(fen: str, info: SearchInfo, config: DataGenConfig) -> bool:
    """Filters out positions a static evaluation has no business judging."""
    if info.is_mate:
        return False
    if info.score_centipawns is None:
        return False
    if abs(info.score_centipawns) > config.max_absolute_score:
        return False

    # A capture as best move means the position is mid-exchange; the network
    # would be asked to predict the result of a sequence the search resolves.
    # Detecting that needs move context this function does not have yet.
    # TODO(nnue): pass the board so captures and checks can be filtered here.
    del fen
    return True


def play_game(engine: UciEngine, config: DataGenConfig, rng: random.Random) -> GameRecord:
    """Plays one self-play game and returns its labelled positions.

    TODO(nnue): implement. Needs legal move generation, game-over detection,
    and the ability to apply a random opening; see the module docstring.
    """
    del engine, config, rng
    raise NotImplementedError(
        "self-play data generation is not implemented yet; see the module docstring "
        "for the two bootstrapping options and what play_game still needs"
    )


def iter_samples(config: DataGenConfig) -> Iterator[str]:
    """Streams dataset lines until the target position count is reached."""
    rng = random.Random(config.seed)
    written = 0

    with UciEngine(config.engine_path) as engine:
        while written < config.target_positions:
            engine.new_game()
            game = play_game(engine, config, rng)

            for fen, info in game.positions:
                if not should_keep(fen, info, config):
                    continue
                white_to_move = fen.split()[1] == "w"
                result = result_for_side_to_move(game.result, white_to_move)
                assert info.score_centipawns is not None
                yield format_sample(fen, info.score_centipawns, result)
                written += 1
                if written >= config.target_positions:
                    return


def generate(config: DataGenConfig) -> Path:
    """Runs a generation pass and writes the dataset."""
    config.output_path.parent.mkdir(parents=True, exist_ok=True)
    with config.output_path.open("w", encoding="utf-8") as handle:
        handle.write(f"# NeraChess NNUE training data, depth {config.depth}\n")
        if config.notes:
            handle.write(f"# {config.notes}\n")
        for line in iter_samples(config):
            handle.write(line + "\n")
    return config.output_path


def shard(lines: Sequence[str], shard_count: int) -> list[list[str]]:
    """Splits lines round-robin, so every shard sees the same game variety."""
    if shard_count < 1:
        raise ValueError("shard count must be positive")
    shards: list[list[str]] = [[] for _ in range(shard_count)]
    for index, line in enumerate(lines):
        shards[index % shard_count].append(line)
    return shards
