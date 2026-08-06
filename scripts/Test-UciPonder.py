#!/usr/bin/env python3

import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path


BESTMOVE_PATTERN = re.compile(
    r"^bestmove ([a-h][1-8][a-h][1-8][qrbn]?) ponder ([a-h][1-8][a-h][1-8][qrbn]?)$"
)


class UciEngine:
    def __init__(self, executable: Path) -> None:
        self.lines: queue.Queue[str] = queue.Queue()
        self.transcript: list[str] = []
        self.process = subprocess.Popen(
            [str(executable)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self) -> None:
        assert self.process.stdout is not None
        for raw_line in self.process.stdout:
            line = raw_line.rstrip("\r\n")
            self.transcript.append(f"< {line}")
            self.lines.put(line)

    def send(self, command: str) -> None:
        if self.process.poll() is not None:
            raise AssertionError(f"engine exited with code {self.process.returncode}")
        assert self.process.stdin is not None
        self.transcript.append(f"> {command}")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def wait_for(self, predicate, description: str, timeout: float = 3.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"timed out waiting for {description}")
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty as error:
                raise AssertionError(f"timed out waiting for {description}") from error
            if predicate(line):
                return line

    def assert_no_bestmove(self, duration: float) -> None:
        deadline = time.monotonic() + duration
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                return
            if line.startswith("bestmove "):
                raise AssertionError(f"engine emitted {line!r} while it should be pondering")

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                self.send("quit")
                self.process.wait(timeout=2.0)
            except (AssertionError, BrokenPipeError, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2.0)


def run_test(executable: Path) -> None:
    engine = UciEngine(executable)
    try:
        engine.send("uci")
        option_lines: list[str] = []
        while True:
            line = engine.wait_for(lambda _: True, "UCI handshake", timeout=10.0)
            if line == "uciok":
                break
            option_lines.append(line)
        if "option name Ponder type check default false" not in option_lines:
            raise AssertionError("engine did not advertise the standard Ponder option")

        engine.send("setoption name Ponder value true")
        engine.send("isready")
        engine.wait_for(lambda line: line == "readyok", "readyok")

        engine.send("position startpos")
        engine.send("go depth 4")
        book_move = engine.wait_for(
            lambda line: line.startswith("bestmove "), "opening-book bestmove"
        )
        book_match = BESTMOVE_PATTERN.fullmatch(book_move)
        if book_match is None or book_match.group(1) != "e2e4":
            raise AssertionError(f"opening book did not provide its expected ponder move: {book_move!r}")

        engine.send("setoption name OwnBook value false")
        engine.send("position startpos")
        engine.send("go depth 4")
        first = engine.wait_for(lambda line: line.startswith("bestmove "), "normal bestmove")
        match = BESTMOVE_PATTERN.fullmatch(first)
        if match is None:
            raise AssertionError(f"normal search did not provide a ponder move: {first!r}")
        best_move, predicted_reply = match.groups()

        engine.send(f"position startpos moves {best_move} {predicted_reply}")
        engine.send("go ponder movetime 300")
        engine.assert_no_bestmove(0.35)
        engine.send("isready")
        engine.wait_for(lambda line: line == "readyok", "readyok during ponder")
        engine.send("ponderhit")
        engine.assert_no_bestmove(0.08)
        engine.wait_for(lambda line: line.startswith("bestmove "), "bestmove after ponderhit")
        engine.assert_no_bestmove(0.1)

        engine.send("position startpos moves e2e4 e7e5")
        engine.send("go ponder depth 1")
        engine.assert_no_bestmove(0.15)
        engine.send("ponderhit")
        engine.wait_for(
            lambda line: line.startswith("bestmove "),
            "bestmove after completed ponderhit",
            timeout=1.0,
        )
        engine.assert_no_bestmove(0.1)

        engine.send("position startpos moves e2e4 e7e5")
        engine.send("go ponder depth 1")
        engine.assert_no_bestmove(0.2)
        engine.send("stop")
        engine.wait_for(
            lambda line: line.startswith("bestmove "),
            "bestmove after completed ponder stop",
            timeout=1.0,
        )
        engine.assert_no_bestmove(0.1)

        engine.send("position startpos")
        engine.send("go depth 3")
        engine.wait_for(lambda line: line.startswith("bestmove "), "bestmove after stopped ponder")
        engine.assert_no_bestmove(0.1)

        engine.send("position startpos")
        engine.send("go ponder searchmoves a1a1")
        engine.assert_no_bestmove(0.15)
        engine.send("ponderhit")
        invalid_root = engine.wait_for(
            lambda line: line.startswith("bestmove "), "empty ponder search result"
        )
        if invalid_root != "bestmove 0000":
            raise AssertionError(f"unexpected empty ponder search result: {invalid_root!r}")

        engine.send("position startpos moves e2e4 e7e5")
        engine.send("go ponder depth 1")
        engine.assert_no_bestmove(0.15)
        engine.send("quit")
        engine.process.wait(timeout=5.0)
        engine.reader.join(timeout=1.0)
        engine.assert_no_bestmove(0.1)
    except Exception:
        print("UCI ponder transcript:", file=sys.stderr)
        print("\n".join(engine.transcript), file=sys.stderr)
        raise
    finally:
        engine.close()


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <UCI engine>", file=sys.stderr)
        return 2
    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        print(f"UCI engine does not exist: {executable}", file=sys.stderr)
        return 2
    run_test(executable)
    print("UCI pondering regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
