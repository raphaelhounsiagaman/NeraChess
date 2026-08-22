#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: Run-Match.sh CANDIDATE_DIRECTORY BASELINE_DIRECTORY OPENING_BOOK RESULTS_DIRECTORY

Configuration is read from environment variables. See docs/Strength-Testing.md.
EOF
}

if [[ $# -ne 4 ]]; then
  usage
  exit 2
fi

candidate_directory="$(realpath "$1")"
baseline_directory="$(realpath "$2")"
opening_book="$(realpath "$3")"
results_directory="$4"

fastchess_bin="${FASTCHESS_BIN:-fastchess}"
time_control="${STRENGTH_TC:-10+0.1}"
concurrency="${STRENGTH_CONCURRENCY:-2}"
max_pairs="${STRENGTH_MAX_PAIRS:-125}"
hash_mb="${STRENGTH_HASH_MB:-128}"
threads="${STRENGTH_THREADS:-1}"
seed="${STRENGTH_SEED:-1}"
allow_network_change="${ALLOW_NETWORK_CHANGE:-false}"

candidate="${candidate_directory}/NeraChessUCI"
baseline="${baseline_directory}/NeraChessUCI"

for executable in "${candidate}" "${baseline}"; do
  if [[ ! -x "${executable}" ]]; then
    echo "Engine is missing or not executable: ${executable}" >&2
    exit 1
  fi
done

if [[ ! -f "${opening_book}" ]]; then
  echo "Opening book does not exist: ${opening_book}" >&2
  exit 1
fi

if ! command -v "${fastchess_bin}" >/dev/null 2>&1 && [[ ! -x "${fastchess_bin}" ]]; then
  echo "fastchess was not found: ${fastchess_bin}" >&2
  exit 1
fi

positive_integer='^[1-9][0-9]*$'
time_control_pattern='^[0-9]+([.][0-9]+)?\+[0-9]+([.][0-9]+)?$'

for name_and_value in \
  "concurrency:${concurrency}" \
  "max pairs:${max_pairs}" \
  "hash size:${hash_mb}" \
  "threads:${threads}" \
  "seed:${seed}"; do
  name="${name_and_value%%:*}"
  value="${name_and_value#*:}"
  if [[ ! "${value}" =~ ${positive_integer} ]]; then
    echo "Invalid ${name}: ${value}" >&2
    exit 1
  fi
done

if [[ ! "${time_control}" =~ ${time_control_pattern} ]]; then
  echo "Invalid time control '${time_control}'; use a value such as 10+0.1." >&2
  exit 1
fi

candidate_network="${candidate_directory}/Resources/NNUE/nera.nnue"
baseline_network="${baseline_directory}/Resources/NNUE/nera.nnue"
if [[ ! -f "${candidate_network}" ]] || [[ ! -f "${baseline_network}" ]]; then
  echo "Both builds must contain Resources/NNUE/nera.nnue." >&2
  exit 1
fi

candidate_network_hash="$(sha256sum "${candidate_network}" | cut -d' ' -f1)"
baseline_network_hash="$(sha256sum "${baseline_network}" | cut -d' ' -f1)"
if [[ "${candidate_network_hash}" != "${baseline_network_hash}" && "${allow_network_change}" != "true" ]]; then
  cat >&2 <<EOF
Candidate and baseline contain different NNUE networks.
candidate: ${candidate_network_hash}
baseline:  ${baseline_network_hash}

Set ALLOW_NETWORK_CHANGE=true only when the network change is intentional.
EOF
  exit 1
fi

if [[ -e "${results_directory}" ]] && [[ -n "$(find "${results_directory}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "Results directory must be empty: ${results_directory}" >&2
  exit 1
fi
mkdir -p "${results_directory}"
results_directory="$(realpath "${results_directory}")"

{
  echo "candidate_revision=$(sed -n 's/^revision=//p' "${candidate_directory}/build-manifest.txt")"
  echo "baseline_revision=$(sed -n 's/^revision=//p' "${baseline_directory}/build-manifest.txt")"
  echo "candidate_network_sha256=${candidate_network_hash}"
  echo "baseline_network_sha256=${baseline_network_hash}"
  echo "opening_book=${opening_book}"
  echo "opening_book_sha256=$(sha256sum "${opening_book}" | cut -d' ' -f1)"
  echo "time_control=${time_control}"
  echo "concurrency=${concurrency}"
  echo "max_pairs=${max_pairs}"
  echo "threads=${threads}"
  echo "hash_mb=${hash_mb}"
  echo "test=fixed_games"
  echo "opening_seed=${seed}"
  "${fastchess_bin}" -version 2>&1 | sed 's/^/fastchess=/'
} | tee "${results_directory}/configuration.txt"

"${fastchess_bin}" \
  -engine "cmd=${candidate}" name=Candidate dir="${candidate_directory}" \
    option.OwnBook=false option.Hash="${hash_mb}" option.Threads="${threads}" \
  -engine "cmd=${baseline}" name=Baseline dir="${baseline_directory}" \
    option.OwnBook=false option.Hash="${hash_mb}" option.Threads="${threads}" \
  -each proto=uci tc="${time_control}" \
  -openings file="${opening_book}" format=epd order=random \
  -srand "${seed}" \
  -rounds "${max_pairs}" \
  -repeat \
  -concurrency "${concurrency}" \
  -draw movenumber=40 movecount=8 score=10 \
  -resign movecount=6 score=800 twosided=true \
  -maxmoves 300 \
  -config outname="${results_directory}/fastchess-config.json" \
  -recover \
  -report penta=true \
  -ratinginterval 10 \
  -pgnout file="${results_directory}/games.pgn" notation=san append=false \
    nodes=true seldepth=true nps=true hashfull=true timeleft=true \
  2>&1 | tee "${results_directory}/fastchess.log"
