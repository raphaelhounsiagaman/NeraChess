#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 OUTPUT_DIRECTORY" >&2
  exit 2
fi

output_directory="$1"
fastchess_revision="ccb85325b1db322658687b8be8cfe9f54c495840"
book_url="https://github.com/official-stockfish/books/raw/master/UHO_4060_v4.epd.zip"
book_archive_sha256="a97424c5b98b42f8c27ff450f0681ad11696148548c975752350e98417ead11d"

if [[ -e "${output_directory}" ]] && [[ -n "$(find "${output_directory}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "Output directory must be empty: ${output_directory}" >&2
  exit 1
fi

mkdir -p "${output_directory}/bin" "${output_directory}/books"
output_directory="$(realpath "${output_directory}")"

git clone --quiet https://github.com/Disservin/fastchess.git \
  "${output_directory}/fastchess-source"
git -C "${output_directory}/fastchess-source" checkout --quiet --detach \
  "${fastchess_revision}"
make -C "${output_directory}/fastchess-source" -j"$(nproc)"
install -m 0755 "${output_directory}/fastchess-source/fastchess" \
  "${output_directory}/bin/fastchess"

curl --location --fail --silent --show-error \
  --output "${output_directory}/book.zip" "${book_url}"
echo "${book_archive_sha256}  ${output_directory}/book.zip" | sha256sum --check
unzip -p "${output_directory}/book.zip" UHO_4060_v4.epd \
  > "${output_directory}/books/UHO_4060_v4.epd"

"${output_directory}/bin/fastchess" -version
