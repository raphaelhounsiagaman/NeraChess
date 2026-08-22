#!/usr/bin/env bash

set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run this script as root: sudo bash $0" >&2
  exit 1
fi

runner_user="chesstest"
testing_root="/data/chess/testing"
fastchess_revision="ccb85325b1db322658687b8be8cfe9f54c495840"
book_url="https://github.com/official-stockfish/books/raw/master/UHO_4060_v4.epd.zip"
book_archive_sha256="a97424c5b98b42f8c27ff450f0681ad11696148548c975752350e98417ead11d"

apt-get update
apt-get install --yes build-essential ca-certificates curl git unzip

if ! id "${runner_user}" >/dev/null 2>&1; then
  useradd --system --create-home --home-dir "/var/lib/${runner_user}" \
    --shell /usr/sbin/nologin "${runner_user}"
fi

install -d -o "${runner_user}" -g "${runner_user}" \
  "${testing_root}" \
  "${testing_root}/actions" \
  "${testing_root}/books" \
  "${testing_root}/tools" \
  "${testing_root}/tools/bin"

fastchess_source="${testing_root}/tools/fastchess-source"
if [[ ! -d "${fastchess_source}/.git" ]]; then
  sudo -u "${runner_user}" git clone https://github.com/Disservin/fastchess.git "${fastchess_source}"
fi
sudo -u "${runner_user}" git -C "${fastchess_source}" fetch --prune origin
sudo -u "${runner_user}" git -C "${fastchess_source}" checkout --detach "${fastchess_revision}"
sudo -u "${runner_user}" make -C "${fastchess_source}" -j"$(nproc)"
install -o "${runner_user}" -g "${runner_user}" -m 0755 \
  "${fastchess_source}/fastchess" "${testing_root}/tools/bin/fastchess"

temporary_directory="$(mktemp -d)"
trap 'rm -rf "${temporary_directory}"' EXIT
curl --location --fail --show-error \
  --output "${temporary_directory}/book.zip" "${book_url}"
echo "${book_archive_sha256}  ${temporary_directory}/book.zip" | sha256sum --check
unzip -p "${temporary_directory}/book.zip" UHO_4060_v4.epd \
  > "${temporary_directory}/UHO_4060_v4.epd"
install -o "${runner_user}" -g "${runner_user}" -m 0644 \
  "${temporary_directory}/UHO_4060_v4.epd" \
  "${testing_root}/books/UHO_4060_v4.epd"

cat <<EOF

The strength-test dependencies are ready.

fastchess: ${testing_root}/tools/bin/fastchess
openings:  ${testing_root}/books/UHO_4060_v4.epd
work:      ${testing_root}/actions

Now register the GitHub Actions runner by following docs/Strength-Testing.md.
EOF
