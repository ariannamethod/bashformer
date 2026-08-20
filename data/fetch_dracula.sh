#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
OUT=${1:-"$ROOT/dracula.txt"}
DRACULA_COMMIT=9fe2eb98fccd637e904a0e638d08fa7da26e9650
URL="https://raw.githubusercontent.com/ariannamethod/nanoGPT-notorch/$DRACULA_COMMIT/dracula.txt"

if [[ -s $OUT ]]; then
    printf 'corpus already present: %s\n' "$OUT"
    exit 0
fi

if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 "$URL" -o "$OUT.tmp"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$OUT.tmp" "$URL"
else
    printf 'need curl or wget to fetch Dracula\n' >&2
    exit 127
fi
mv "$OUT.tmp" "$OUT"
printf 'fetched Dracula: %s (%s bytes)\n' "$OUT" "$(wc -c < "$OUT")"
