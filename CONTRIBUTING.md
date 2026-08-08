# Contributing to NeraChess

Thank you for taking the time to improve NeraChess. Small, focused changes with a clear reason and reproducible verification are the easiest to review.

## Development workflow

1. Generate project files with the setup script for your platform.
2. Build the Debug configuration while developing.
3. Run `NeraChessTests` after every engine, rules, or search change.
4. Build Release and run the application and UCI smoke tests before opening a pull request.

The exact commands for macOS, Windows, and Linux are documented in [README.md](README.md#build).

## Tests

New engine behavior should include a regression test in `NeraChessTests/src/main.cpp`. Prefer established perft positions or a minimal FEN that isolates the rule being tested. Tests must be deterministic and must not require network access.

For desktop changes, verify both a normal launch and `--smoke-test`. UCI changes should cover the relevant command sequence and preserve asynchronous `stop` and pondering behavior. Pull requests are expected to pass every GitHub Actions job.

## Code style

- Use C++23 and the checked-in `.clang-format` configuration.
- Preserve the separation between chess rules, search, protocol adapters, and presentation code.
- Prefer RAII and standard-library ownership types over manual resource management.
- Avoid hidden global state, unchecked input, and platform-specific behavior outside the build/runtime abstraction layers.
- Keep generated Premake output, local IDE settings, and build artifacts out of commits.

Format project-owned C++ files with:

```sh
find ApplicationCore/src NeraChessApp/src NeraChessEngine/src NeraChessSearch/src NeraChessTests/src NeraChessUCI/src \
  -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i
```

## Commit and pull-request scope

Write commit messages in the imperative mood and explain behavior changes in the pull-request description. If a change affects move generation or evaluation strength, include the relevant test result, perft count, benchmark, or game sample rather than relying on an estimated rating.
