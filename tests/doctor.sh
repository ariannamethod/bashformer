#!/usr/bin/env bash
# The doctor itself must exercise the exact forge-facing Notorch ABI.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/bashformer-doctor.$$
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$TMP"
${CC:-cc} -O2 -Wall -Wextra -Werror -std=c11 -I"$ROOT/tests/stub" \
    "$ROOT/tools/notorch_doctor.c" "$ROOT/tests/stub/notorch_stub.c" -lm -o "$TMP/doctor"
out=$("$TMP/doctor")
[[ $out == 'notorch-doctor: OK loss=4.500000 gqa=2:1 rope=500000 chuck=OK' ]]
printf 'doctor: forge-facing GQA/RoPE/SwiGLU/backward/Chuck ABI probe passes\n'
