#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: $0 SOURCE_DIRECTORY OUTPUT_DIRECTORY" >&2
}

if [[ $# -ne 2 ]]; then
  usage
  exit 2
fi

source_directory="$(realpath "$1")"
output_directory="$2"

if [[ ! -f "${source_directory}/Build-NeraChess.lua" ]]; then
  echo "Not a NeraChess source directory: ${source_directory}" >&2
  exit 1
fi

if [[ -e "${output_directory}" ]] && [[ -n "$(find "${output_directory}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "Output directory must be empty: ${output_directory}" >&2
  exit 1
fi

if [[ ! "${NERACHESS_BUILD_JOBS:-}" =~ ^[1-9][0-9]*$ ]]; then
  if [[ -n "${NERACHESS_BUILD_JOBS:-}" ]]; then
    echo "NERACHESS_BUILD_JOBS must be a positive integer." >&2
    exit 1
  fi
  NERACHESS_BUILD_JOBS="$(nproc)"
fi

mkdir -p "${output_directory}"
output_directory="$(realpath "${output_directory}")"

pushd "${source_directory}" >/dev/null
bash ./scripts/Setup-Linux.sh gmake
make -C NeraChessEngine config=release -j"${NERACHESS_BUILD_JOBS}"
make -C NeraChessNNUE config=release -j"${NERACHESS_BUILD_JOBS}"
make -C NeraChessSearch config=release -j"${NERACHESS_BUILD_JOBS}"
make -C NeraChessUCI config=release -j"${NERACHESS_BUILD_JOBS}"

engine_directory="${source_directory}/bin/Release/NeraChessUCI"
if [[ ! -x "${engine_directory}/NeraChessUCI" ]]; then
  echo "The build did not produce ${engine_directory}/NeraChessUCI" >&2
  exit 1
fi

cp -a "${engine_directory}/." "${output_directory}/"

{
  echo "revision=$(git rev-parse HEAD)"
  echo "compiler=$(g++ --version | head -n 1)"
  echo "engine_sha256=$(sha256sum "${output_directory}/NeraChessUCI" | cut -d' ' -f1)"
  if [[ -f "${output_directory}/Resources/NNUE/nera.nnue" ]]; then
    echo "network_sha256=$(sha256sum "${output_directory}/Resources/NNUE/nera.nnue" | cut -d' ' -f1)"
  fi
} > "${output_directory}/build-manifest.txt"
popd >/dev/null

echo "Built $(grep '^revision=' "${output_directory}/build-manifest.txt")"
