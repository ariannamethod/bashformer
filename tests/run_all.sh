#!/usr/bin/env bash
# Run independent release gates concurrently; print logs in stable order.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/bashformer-tests.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

tests=(parity method bootstrap forge stateful dynamics spa doctor)
pids=()
for name in "${tests[@]}"; do
    bash "$ROOT/tests/$name.sh" >"$TMP/$name.log" 2>&1 &
    pids+=("$!")
done

failed=0
for i in "${!tests[@]}"; do
    name=${tests[i]}
    if ! wait "${pids[i]}"; then failed=1; fi
    printf '%s' "$(<"$TMP/$name.log")"
    printf '\n'
done
exit "$failed"
