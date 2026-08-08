#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
action="${1:-gmake}"
premake="${project_dir}/vendor/Premake/Linux/premake5"

if [[ "${action}" != "gmake" && "${action}" != "gmake2" ]]; then
  echo "Usage: $0 [gmake|gmake2]" >&2
  exit 2
fi

if [[ ! -f "${premake}" ]]; then
  echo "Bundled Linux Premake executable was not found." >&2
  exit 1
fi

chmod u+x "${premake}"
cd "${project_dir}"
"${premake}" --fatal --file=Build-NeraChess.lua "${action}"
