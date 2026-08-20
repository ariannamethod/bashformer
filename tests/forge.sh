#!/usr/bin/env bash
# End-to-end control-flow smoke for src/train.c using the ABI contract stub.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/bashformer-forge.$$
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$TMP"

# The suite exercises control flow and the export contract, not the shipping
# geometry: a 9M-parameter export would take minutes just to load into Bash.
# The forge geometry is overridable, so the test compiles a small one and then
# checks the export against the geometry it actually built.
GEO=(-DBF_DIM=32 -DBF_LAYERS=2 -DBF_QHEADS=4 -DBF_KVHEADS=1 -DBF_FFN=64 -DBF_CTX=64)
${CC:-cc} -O1 -Wall -Wextra -Werror -std=c11 -I"$ROOT/tests/stub" "${GEO[@]}" \
    "$ROOT/src/train.c" "$ROOT/tests/stub/notorch_stub.c" -lm -o "$TMP/forge"

"$TMP/forge" --corpus "$ROOT/data/tiny.txt" --steps 1 --lr 0.001 --out "$TMP/model" > "$TMP/forge.log"
[[ -s "$TMP/model.bin" && -s "$TMP/model.bfw" && -s "$TMP/model.meta" ]]
# The exported geometry must match the forge source, not a number frozen into
# this test - otherwise changing BF_LAYERS silently breaks the suite instead of
# proving the export followed.
for key in LAYERS DIM FFN CTX VOCAB QHEADS KVHEADS; do
    want=''
    for g in "${GEO[@]}"; do [[ $g == "-DBF_${key}="* ]] && want=${g##*=}; done
    [[ -n $want ]] || want=$(sed -n "s/^#define BF_${key} \\([0-9]*\\)$/\\1/p" "$ROOT/src/train.c")
    [[ -n $want ]] || { printf 'forge: cannot resolve BF_%s\n' "$key" >&2; exit 1; }
    grep -q "^M $key $want\$" "$TMP/model.bfw" || {
        printf 'forge: exported %s does not match BF_%s=%s\n' "$key" "$key" "$want" >&2
        exit 1
    }
done
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
