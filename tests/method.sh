#!/usr/bin/env bash
# Exact parity and ablation gates for the stateless Method overlay.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
B="$ROOT/bashformer.sh"
C="$ROOT/build/reference"
W="$ROOT/weights/fixture.bfw"
WP="$ROOT/weights/fixture-prod.bfw"
PROMPT='AB'
TMP=${TMPDIR:-/tmp}/bashformer-method.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

compare_trace() {
    local name=$1; shift
    "$B" --weights "$W" --prompt "$PROMPT" --trace --next-id "$@" \
        >"$TMP/$name.b.out" 2>"$TMP/$name.b.trace"
    "$C" --weights "$W" --prompt "$PROMPT" --trace --next-id "$@" \
        >"$TMP/$name.c.out" 2>"$TMP/$name.c.trace"
    cmp "$TMP/$name.c.out" "$TMP/$name.b.out"
    cmp "$TMP/$name.c.trace" "$TMP/$name.b.trace"
}

# Each operator and the combined pipeline must match the independent C port exactly.
compare_trace destiny --method --destiny 0.75 --prophecy 32
compare_trace pain --method --pain 0.8
compare_trace attention --method --focus 0.9 --spread 0.1
compare_trace combined --method --destiny 0.6 --prophecy 64 \
    --pain 0.4 --focus 1 --spread 0.25

# One combined case crosses the actual v0 geometry as well; the exhaustive matrix above
# stays on the tiny fixture so the default test suite remains usable on ordinary shells.
"$B" --weights "$WP" --prompt 'The ' --trace --next-id --velocity walk \
    --destiny 0.6 --prophecy 21 --pain 0.3 --focus 0.8 --spread 0.1 \
    >"$TMP/prod.b.out" 2>"$TMP/prod.b.trace"
"$C" --weights "$WP" --prompt 'The ' --trace --next-id --velocity walk \
    --destiny 0.6 --prophecy 21 --pain 0.3 --focus 0.8 --spread 0.1 \
    >"$TMP/prod.c.out" 2>"$TMP/prod.c.trace"
cmp "$TMP/prod.c.out" "$TMP/prod.b.out"
cmp "$TMP/prod.c.trace" "$TMP/prod.b.trace"

# The operators must actually move the distribution rather than only parse flags.
"$B" --weights "$W" --prompt "$PROMPT" --trace --next-id \
    >"$TMP/vanilla.out" 2>"$TMP/vanilla.trace"
for name in destiny pain attention combined; do
    if cmp -s "$TMP/vanilla.trace" "$TMP/$name.b.trace"; then
        printf '%s Method operator produced the vanilla trace\n' "$name" >&2
        exit 1
    fi
    grep -q '^TRACE method ' "$TMP/$name.b.trace"
    grep -q ' method_raw=' "$TMP/$name.b.trace"
done

# FIELD OFF is a real gate. Explicitly disabling it restores the exact vanilla trace,
# even if non-neutral coefficients were supplied earlier on the command line.
"$B" --weights "$W" --prompt "$PROMPT" --trace --next-id \
    --destiny 1 --pain 1 --focus 1 --spread 1 --field off \
    >"$TMP/off.out" 2>"$TMP/off.trace"
cmp "$TMP/vanilla.out" "$TMP/off.out"
cmp "$TMP/vanilla.trace" "$TMP/off.trace"

# VELOCITY modulates positive sampling temperature. A built-in xorshift32 RNG makes
# seeded sampling byte-identical between C and Bash without relying on $RANDOM.
velocities=(raw nomove walk run breathe)
for velocity in "${velocities[@]}"; do
    "$B" --weights "$W" --prompt AB --tokens 12 --generated-only \
        --temperature 1 --seed 1 --method --velocity "$velocity" >"$TMP/$velocity.b.gen"
    "$C" --weights "$W" --prompt AB --tokens 12 --generated-only \
        --temperature 1 --seed 1 --method --velocity "$velocity" >"$TMP/$velocity.c.gen"
    cmp "$TMP/$velocity.c.gen" "$TMP/$velocity.b.gen"
done
if cmp -s "$TMP/raw.b.gen" "$TMP/nomove.b.gen"; then
    printf 'velocity did not alter seeded sampling\n' >&2
    exit 1
fi

# The Method layer is still Bash-builtins-only at runtime.
expected=$("$C" --weights "$W" --prompt AB --next-id --destiny 0.7 --pain 0.2)
actual=$(PATH=/definitely-empty "$BASH" "$B" --weights "$W" --prompt AB \
    --next-id --destiny 0.7 --pain 0.2)
[[ $actual == "$expected" ]]

# Public bounds are fail-closed.
invalid=(
    '--destiny 1.01'
    '--pain -0.1'
    '--focus 2'
    '--spread nope'
    '--prophecy 0'
    '--prophecy 65'
    '--velocity teleport'
    '--seed -1'
)
for spec in "${invalid[@]}"; do
    read -r -a args <<< "$spec"
    if "$B" --weights "$W" --prompt A --next-id "${args[@]}" >/dev/null 2>&1; then
        printf 'accepted invalid Method arguments: %s\n' "$spec" >&2
        exit 1
    fi
done

printf 'method: destiny, pain, attention, field gate, and 5 velocity modes match C exactly; seeded sampling and bounds pass\n'
