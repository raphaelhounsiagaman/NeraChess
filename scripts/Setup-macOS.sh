#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
action="${1:-xcode4}"

if [[ "${action}" != "xcode4" && "${action}" != "gmake" ]]; then
  echo "Usage: $0 [xcode4|gmake]" >&2
  exit 2
fi

for dependency in premake5 brew pkg-config; do
  if ! command -v "${dependency}" >/dev/null 2>&1; then
    echo "Missing required command: ${dependency}" >&2
    exit 1
  fi
done

if ! pkg-config --exists sdl2 SDL2_image SDL2_mixer; then
  echo "Missing SDL dependencies. Run: brew install sdl2-compat sdl2_image sdl2_mixer" >&2
  exit 1
fi

cd "${project_dir}"
premake5 --fatal --file=Build-NeraChess.lua "${action}"
