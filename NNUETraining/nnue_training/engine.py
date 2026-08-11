"""Minimal UCI client for talking to a built NeraChessUCI binary.

Used by :mod:`nnue_training.datagen` to label positions and by
:mod:`nnue_training.verify` to compare the engine's evaluation against this
package's reference forward pass. Standard library only.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


class UciError(RuntimeError):
    """Raised when the engine fails to answer as the protocol requires."""


@dataclass
class SearchInfo:
    """What a search reported for one position."""

    best_move: str
    score_centipawns: int | None = None
    mate_in: int | None = None
    depth: int = 0

    @property
    def is_mate(self) -> bool:
        return self.mate_in is not None


class UciEngine:
    """A NeraChessUCI subprocess driven over stdin and stdout.

    Use as a context manager so the process is always shut down::

        with UciEngine("./bin/Release/NeraChessUCI/NeraChessUCI") as engine:
            print(engine.evaluate("startpos"))
    """

    def __init__(self, path: str | Path, eval_file: str | Path | None = None) -> None:
        self.path = Path(path)
        self.eval_file = Path(eval_file) if eval_file else None
        self._process: subprocess.Popen[str] | None = None

    def __enter__(self) -> "UciEngine":
        self.start()
        return self

    def __exit__(self, *exception: object) -> None:
        self.close()

    def start(self) -> None:
        if self._process is not None:
            return
        if not self.path.exists():
            raise UciError(
                f"engine binary not found at {self.path}; build it first, for example "
                "`make -C NeraChessUCI config=release`"
            )

        self._process = subprocess.Popen(
            [str(self.path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._send("uci")
        self._read_until("uciok")

        # The opening book would answer instead of the search, which is not
        # what data generation or evaluation checks want to measure.
        self._send("setoption name OwnBook value false")
        if self.eval_file is not None:
            self._send(f"setoption name EvalFile value {self.eval_file}")
        self.wait_ready()

    def close(self) -> None:
        if self._process is None:
            return
        try:
            self._send("quit")
            self._process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            self._process.kill()
        finally:
            self._process = None

    # -- protocol ---------------------------------------------------------

    def _require_process(self) -> subprocess.Popen[str]:
        if self._process is None or self._process.stdin is None or self._process.stdout is None:
            raise UciError("engine is not running")
        return self._process

    def _send(self, command: str) -> None:
        process = self._require_process()
        assert process.stdin is not None
        process.stdin.write(command + "\n")
        process.stdin.flush()

    def _lines(self) -> Iterator[str]:
        process = self._require_process()
        assert process.stdout is not None
        for line in process.stdout:
            yield line.strip()

    def _read_until(self, terminator: str) -> list[str]:
        collected: list[str] = []
        for line in self._lines():
            collected.append(line)
            if line == terminator or line.startswith(terminator + " "):
                return collected
        raise UciError(f"engine exited before sending {terminator!r}")

    def wait_ready(self) -> None:
        self._send("isready")
        self._read_until("readyok")

    # -- commands ---------------------------------------------------------

    def new_game(self) -> None:
        self._send("ucinewgame")
        self.wait_ready()

    def set_position(self, fen: str | None = None, moves: list[str] | None = None) -> None:
        command = "position " + ("startpos" if fen is None else f"fen {fen}")
        if moves:
            command += " moves " + " ".join(moves)
        self._send(command)

    def evaluate(self, fen: str | None = None) -> int:
        """Static evaluation in centipawns, from the ``eval`` command."""
        self.set_position(fen)
        self._send("eval")
        for line in self._lines():
            if line.startswith("info string evaluation "):
                return int(line.split()[3])
            if line.startswith("bestmove"):
                break
        raise UciError("engine did not report an evaluation")

    def search(
        self,
        fen: str | None = None,
        depth: int | None = None,
        nodes: int | None = None,
        movetime_ms: int | None = None,
    ) -> SearchInfo:
        """Searches one position and returns its best move and score."""
        if depth is None and nodes is None and movetime_ms is None:
            raise ValueError("a search needs a depth, node, or time limit")

        self.set_position(fen)
        command = "go"
        if depth is not None:
            command += f" depth {depth}"
        if nodes is not None:
            command += f" nodes {nodes}"
        if movetime_ms is not None:
            command += f" movetime {movetime_ms}"
        self._send(command)

        info = SearchInfo(best_move="0000")
        for line in self._lines():
            if line.startswith("info depth "):
                info = _parse_info(line, info)
            elif line.startswith("bestmove"):
                parts = line.split()
                if len(parts) >= 2:
                    info.best_move = parts[1]
                return info
        raise UciError("engine exited before sending a best move")


def _parse_info(line: str, previous: SearchInfo) -> SearchInfo:
    tokens = line.split()
    info = SearchInfo(
        best_move=previous.best_move,
        score_centipawns=previous.score_centipawns,
        mate_in=previous.mate_in,
        depth=previous.depth,
    )
    for index, token in enumerate(tokens):
        if token == "depth" and index + 1 < len(tokens):
            info.depth = int(tokens[index + 1])
        elif token == "score" and index + 2 < len(tokens):
            kind, value = tokens[index + 1], int(tokens[index + 2])
            if kind == "cp":
                info.score_centipawns = value
                info.mate_in = None
            elif kind == "mate":
                info.mate_in = value
                info.score_centipawns = None
    return info
