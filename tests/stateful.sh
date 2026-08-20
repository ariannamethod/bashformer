#!/usr/bin/env bash
# Stateful Method parity: co-occurrence H-term + BFSOMA3 persistence.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
B="$ROOT/bashformer.sh"
C="$ROOT/build/reference"
W="$ROOT/weights/fixture.bfw"
WP="$ROOT/weights/fixture-prod.bfw"
TMP=${TMPDIR:-/tmp}/bashformer-stateful.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

compare_once() {
    local name=$1 weights=$2 prompt=$3; shift 3
    "$B" --weights "$weights" --prompt "$prompt" --trace --next-id "$@" \
        >"$TMP/$name.b.out" 2>"$TMP/$name.b.trace"
    "$C" --weights "$weights" --prompt "$prompt" --trace --next-id "$@" \
        >"$TMP/$name.c.out" 2>"$TMP/$name.c.trace"
    cmp "$TMP/$name.c.out" "$TMP/$name.b.out"
    cmp "$TMP/$name.c.trace" "$TMP/$name.b.trace"
}

# Live learning: H-term is applied after each newly observed token and must match C exactly.
compare_once h-small "$W" ABCA --hebbian
compare_once h-mixed "$W" ABCABA --hebbian --destiny 0.4 --prophecy 17 --pain 0.25 --focus 0.8 --spread 0.1
compare_once h-prod "$WP" 'The blood ' --hebbian --focus 0.7 --spread 0.15

grep -q 'hebbian=1' "$TMP/h-small.b.trace"
# The H-term must actually move at least one post-first-token logit checksum.
"$B" --weights "$W" --prompt ABCA --trace --next-id >"$TMP/vanilla.out" 2>"$TMP/vanilla.trace"
if cmp -s "$TMP/vanilla.trace" "$TMP/h-small.b.trace"; then
    printf 'hebbian mode produced vanilla trace\n' >&2
    exit 1
fi

# Persistent state: two independent runtimes must serialize byte-identical BFSOMA3.
BS="$TMP/bash.soma"; CS="$TMP/c.soma"
"$B" --weights "$W" --prompt ABCA --next-id --soma "$BS" >"$TMP/s1.b.out"
"$C" --weights "$W" --prompt ABCA --next-id --soma "$CS" >"$TMP/s1.c.out"
cmp "$TMP/s1.c.out" "$TMP/s1.b.out"
cmp "$CS" "$BS"
grep -q '^BFSOMA3$' "$BS"
grep -q '^E ' "$BS"
grep -q '^R ' "$BS"

# A second session loads previous memory, uses it, learns more, and remains exact.
"$B" --weights "$W" --prompt CABA --trace --next-id --soma "$BS" \
    >"$TMP/s2.b.out" 2>"$TMP/s2.b.trace"
"$C" --weights "$W" --prompt CABA --trace --next-id --soma "$CS" \
    >"$TMP/s2.c.out" 2>"$TMP/s2.c.trace"
cmp "$TMP/s2.c.out" "$TMP/s2.b.out"
cmp "$TMP/s2.c.trace" "$TMP/s2.b.trace"
cmp "$CS" "$BS"

# --learn off reads/applies memory but cannot mutate it.
cp "$BS" "$TMP/b.before"; cp "$CS" "$TMP/c.before"
"$B" --weights "$W" --prompt B --trace --next-id --soma "$BS" --learn off \
    >"$TMP/ro.b.out" 2>"$TMP/ro.b.trace"
"$C" --weights "$W" --prompt B --trace --next-id --soma "$CS" --learn off \
    >"$TMP/ro.c.out" 2>"$TMP/ro.c.trace"
cmp "$TMP/ro.c.out" "$TMP/ro.b.out"
cmp "$TMP/ro.c.trace" "$TMP/ro.b.trace"
cmp "$TMP/b.before" "$BS"
cmp "$TMP/c.before" "$CS"

# FIELD OFF is a true state gate: neither H application nor learning modifies the soma.
cp "$BS" "$TMP/off.before"
"$B" --weights "$W" --prompt ABC --next-id --soma "$BS" --field off >/dev/null
cmp "$TMP/off.before" "$BS"

# Loading/saving state is still Bash-builtins-only at inference time.
cp "$BS" "$TMP/pathless.soma"
PATH=/definitely-empty "$BASH" "$B" --weights "$W" --prompt A --next-id \
    --soma "$TMP/pathless.soma" --learn off >/dev/null
cmp "$BS" "$TMP/pathless.soma"

# The state file is parsed as data, never sourced. Malformed or shell-looking records fail closed.
cat >"$TMP/bad.soma" <<'BAD'
BFSOMA2
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 0
M DEBT_Q 0
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
E 1 2 $(touch /tmp/bashformer-should-never-run)
R 0
Z
BAD
rm -f /tmp/bashformer-should-never-run
if "$B" --weights "$W" --prompt A --next-id --soma "$TMP/bad.soma" >/dev/null 2>&1; then
    printf 'accepted malformed/injectable soma\n' >&2
    exit 1
fi
[[ ! -e /tmp/bashformer-should-never-run ]]

printf 'stateful: H-term, live learning, BFSOMA3 persistence/read-only mode, field gate, parser safety, and C parity pass\n'
