#!/usr/bin/env bash
# End-to-end control-flow smoke for src/train.c using the ABI contract stub.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/bashformer-forge.$$
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$TMP"

${CC:-cc} -O1 -Wall -Wextra -Werror -std=c11 -I"$ROOT/tests/stub" \
    "$ROOT/src/train.c" "$ROOT/tests/stub/notorch_stub.c" -lm -o "$TMP/forge"

"$TMP/forge" --corpus "$ROOT/data/tiny.txt" --steps 1 --lr 0.001 --out "$TMP/model" > "$TMP/forge.log"
[[ -s "$TMP/model.bin" && -s "$TMP/model.bfw" && -s "$TMP/model.meta" ]]
grep -q '^M LAYERS 2$' "$TMP/model.bfw"
grep -q '^M ROPE_THETA 500000$' "$TMP/model.bfw"
grep -q '^Z$' "$TMP/model.bfw"

b=$("$ROOT/bashformer.sh" --weights "$TMP/model.bfw" --prompt 'AB' --trace --next-id 2>"$TMP/b.trace")
c=$("$ROOT/build/reference" --weights "$TMP/model.bfw" --prompt 'AB' --trace --next-id 2>"$TMP/c.trace")
[[ $b == "$c" ]]
cmp "$TMP/b.trace" "$TMP/c.trace"

# The exported forge artifact must also accept the opt-in Method overlay without
# changing the BFW1 boundary or reaching outside Bash at runtime.
b=$("$ROOT/bashformer.sh" --weights "$TMP/model.bfw" --prompt 'AB' \
    --trace --next-id --destiny 0.6 --prophecy 21 --pain 0.3 --focus 0.8 --spread 0.1 \
    2>"$TMP/b.method.trace")
c=$("$ROOT/build/reference" --weights "$TMP/model.bfw" --prompt 'AB' \
    --trace --next-id --destiny 0.6 --prophecy 21 --pain 0.3 --focus 0.8 --spread 0.1 \
    2>"$TMP/c.method.trace")
[[ $b == "$c" ]]
cmp "$TMP/b.method.trace" "$TMP/c.method.trace"

printf 'forge: trainer control flow, checkpoint, BFW1 export, vanilla parity, and Method parity pass\n'
