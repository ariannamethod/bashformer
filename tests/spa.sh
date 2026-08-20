#!/usr/bin/env bash
# Sentence Phonon Attention + debt-gated Hebbian plasticity parity/persistence.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
B="$ROOT/bashformer.sh"
C="$ROOT/build/reference"
W="$ROOT/weights/fixture.bfw"
WP="$ROOT/weights/fixture-prod.bfw"
TMP=${TMPDIR:-/tmp}/bashformer-spa.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

compare_run() {
    local name=$1; shift
    "$B" "$@" >"$TMP/$name.b.out" 2>"$TMP/$name.b.trace"
    "$C" "$@" >"$TMP/$name.c.out" 2>"$TMP/$name.c.trace"
    cmp "$TMP/$name.c.out" "$TMP/$name.b.out"
    cmp "$TMP/$name.c.trace" "$TMP/$name.b.trace"
}

# A completed prompt sentence becomes history; the unfinished sentence attends to it.
compare_run connected --weights "$WP" --prompt 'The blood. The ' --trace --next-id --spa
grep -q '^TRACE spa conn=4096 temp=2867 current=1 history=1 sentences=1 alpha=3482 strength=1229$' \
    "$TMP/connected.b.trace" || {
    printf 'SPA did not establish the expected one-sentence field\n' >&2; exit 1;
}

# Two completed sentences make connectedness a genuine distribution over
# history rather than the one-item identity. The value must land strictly
# between zero and Q, and Bash/C must still agree at every trace line.
compare_run multi --weights "$W" --prompt $'A\nB\nA' --trace --next-id --spa
multi_conn=$(grep '^TRACE spa ' "$TMP/multi.b.trace" | tail -1 | sed -E 's/.* conn=([0-9]+).*/\1/')
[[ $multi_conn -gt 0 && $multi_conn -lt 4096 ]]
grep -q 'history=2' "$TMP/multi.b.trace"

# SPA is not decorative: at fixed weights/seed it changes a sampled trajectory.
"$B" --weights "$WP" --prompt 'The blood. The ' --tokens 12 --generated-only \
    --temperature 0.9 --seed 1 >"$TMP/vanilla.out"
"$B" --weights "$WP" --prompt 'The blood. The ' --tokens 12 --generated-only \
    --temperature 0.9 --seed 1 --spa --spa-strength 0.8 >"$TMP/spa.out"
if cmp -s "$TMP/vanilla.out" "$TMP/spa.out"; then
    printf 'SPA did not alter the sampled trajectory\n' >&2
    exit 1
fi
compare_run sampled --weights "$WP" --prompt 'The blood. The ' --tokens 12 --generated-only \
    --temperature 0.9 --seed 1 --spa --spa-strength 0.8

# Debt-gated plasticity must raise the same co-occurrence edge above flat learning.
for mode in flat debt; do
    bs="$TMP/$mode.b.soma"; cs="$TMP/$mode.c.soma"
    "$B" --weights "$W" --prompt AB --next-id --soma "$bs" --plasticity "$mode" --debt 5 \
        >"$TMP/$mode.b.out" 2>"$TMP/$mode.b.trace"
    "$C" --weights "$W" --prompt AB --next-id --soma "$cs" --plasticity "$mode" --debt 5 \
        >"$TMP/$mode.c.out" 2>"$TMP/$mode.c.trace"
    cmp "$TMP/$mode.c.out" "$TMP/$mode.b.out"
    cmp "$TMP/$mode.c.trace" "$TMP/$mode.b.trace"
    cmp "$cs" "$bs"
done
flat_edge=$(awk '$1=="E" && $2==1 && $3==2 {print $4}' "$TMP/flat.b.soma")
debt_edge=$(awk '$1=="E" && $2==1 && $3==2 {print $4}' "$TMP/debt.b.soma")
[[ -n $flat_edge && -n $debt_edge && $debt_edge -gt $flat_edge ]] || {
    printf 'debt-gated plasticity did not strengthen edge 1->2 (%s -> %s)\n' "$flat_edge" "$debt_edge" >&2
    exit 1
}
grep -q '^M PLASTICITY debt$' "$TMP/debt.b.soma"
grep -Eq '^M PLASTICITY_LAST_GAIN_Q [5-8][0-9]{3}$' "$TMP/debt.b.soma"

# Full BFSOMA3: sentence embeddings + current sentence + field/plasticity survive processes.
BS="$TMP/bash.soma"; CS="$TMP/c.soma"
"$B" --weights "$WP" --prompt 'The blood. The ' --tokens 5 --generated-only \
    --temperature 0.8 --seed 42 --soma "$BS" --spa --plasticity debt --debt 3 --trace \
    >"$TMP/s1.b.out" 2>"$TMP/s1.b.trace"
"$C" --weights "$WP" --prompt 'The blood. The ' --tokens 5 --generated-only \
    --temperature 0.8 --seed 42 --soma "$CS" --spa --plasticity debt --debt 3 --trace \
    >"$TMP/s1.c.out" 2>"$TMP/s1.c.trace"
cmp "$TMP/s1.c.out" "$TMP/s1.b.out"
cmp "$TMP/s1.c.trace" "$TMP/s1.b.trace"
cmp "$CS" "$BS"
grep -q '^BFSOMA3$' "$BS"
grep -q '^M SPA_ENABLED 1$' "$BS"
grep -Eq '^M SPA_HISTORY_N [1-8]$' "$BS"
grep -q '^S 0 32 ' "$BS"
grep -q '^C ' "$BS"

# No --spa or --plasticity flags: the second process resumes them from the soma.
"$B" --weights "$WP" --prompt 'Night. The ' --tokens 4 --generated-only \
    --temperature 0.8 --seed 7 --soma "$BS" --trace >"$TMP/s2.b.out" 2>"$TMP/s2.b.trace"
"$C" --weights "$WP" --prompt 'Night. The ' --tokens 4 --generated-only \
    --temperature 0.8 --seed 7 --soma "$CS" --trace >"$TMP/s2.c.out" 2>"$TMP/s2.c.trace"
cmp "$TMP/s2.c.out" "$TMP/s2.b.out"
cmp "$TMP/s2.c.trace" "$TMP/s2.b.trace"
cmp "$CS" "$BS"
grep -q ' plasticity=debt ' "$TMP/s2.b.trace"
grep -q ' spa=1 ' "$TMP/s2.b.trace"

# Read-only and FIELD OFF sessions can query but cannot mutate the entity.
cp "$BS" "$TMP/read-only.soma"; cp "$BS" "$TMP/read-only.before"
"$B" --weights "$WP" --prompt 'The night. ' --tokens 3 --generated-only --temperature 0.8 \
    --soma "$TMP/read-only.soma" --learn off >/dev/null
cmp "$TMP/read-only.before" "$TMP/read-only.soma"
cp "$BS" "$TMP/off.soma"; cp "$BS" "$TMP/off.before"
"$B" --weights "$WP" --prompt 'The night. ' --tokens 3 --generated-only --temperature 0.8 \
    --soma "$TMP/off.soma" --field off >/dev/null
cmp "$TMP/off.before" "$TMP/off.soma"

# Legacy BFSOMA2 migrates to BFSOMA3 with neutral SPA/flat plasticity.
cat >"$TMP/v2.b.soma" <<'V2'
BFSOMA2
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 2
M DEBT_Q 0
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
E 1 2 4096
E 2 1 4096
R 2 1 2
Z
V2
cp "$TMP/v2.b.soma" "$TMP/v2.c.soma"
"$B" --weights "$W" --prompt A --tokens 1 --generated-only --temperature 0 \
    --soma "$TMP/v2.b.soma" >"$TMP/v2.b.out"
"$C" --weights "$W" --prompt A --tokens 1 --generated-only --temperature 0 \
    --soma "$TMP/v2.c.soma" >"$TMP/v2.c.out"
cmp "$TMP/v2.c.out" "$TMP/v2.b.out"
cmp "$TMP/v2.c.soma" "$TMP/v2.b.soma"
grep -q '^BFSOMA3$' "$TMP/v2.b.soma"
grep -q '^M SPA_ENABLED 0$' "$TMP/v2.b.soma"
grep -q '^M SPA_HISTORY_N 0$' "$TMP/v2.b.soma"
grep -q '^M PLASTICITY flat$' "$TMP/v2.b.soma"

# A loaded unfinished sentence may already end at a boundary. It must be committed
# before the first token of the new process, not silently fused with that token.
cat >"$TMP/pending.b.soma" <<'PENDING'
BFSOMA3
M VOCAB 96
M QSHIFT 12
M COOC_TOTAL 0
M DEBT_Q 0
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
M PLASTICITY flat
M PLASTICITY_UPDATES 0
M PLASTICITY_LAST_GAIN_Q 4096
M SPA_ENABLED 1
M SPA_ALPHA_Q 3482
M SPA_STRENGTH_Q 1229
M SPA_HISTORY_MAX 8
M SPA_HISTORY_N 0
M SPA_SENTENCES 0
M SPA_LAST_CONN_Q 0
M SPA_LAST_TEMP_Q 4096
R 0
C 1 15
Z
PENDING
cp "$TMP/pending.b.soma" "$TMP/pending.c.soma"
"$B" --weights "$WP" --prompt A --next-id --soma "$TMP/pending.b.soma" --trace \
    >"$TMP/pending.b.out" 2>"$TMP/pending.b.trace"
"$C" --weights "$WP" --prompt A --next-id --soma "$TMP/pending.c.soma" --trace \
    >"$TMP/pending.c.out" 2>"$TMP/pending.c.trace"
cmp "$TMP/pending.c.out" "$TMP/pending.b.out"
cmp "$TMP/pending.c.trace" "$TMP/pending.b.trace"
cmp "$TMP/pending.c.soma" "$TMP/pending.b.soma"
grep -q '^M SPA_HISTORY_N 1$' "$TMP/pending.b.soma"
grep -q '^M SPA_SENTENCES 1$' "$TMP/pending.b.soma"
grep -q '^S 0 32 ' "$TMP/pending.b.soma"
grep -q '^C 1 34$' "$TMP/pending.b.soma"

# BFSOMA3 remains data, never shell: a shell-looking embedding is rejected, not executed.
cat >"$TMP/bad.soma" <<'BAD'
BFSOMA3
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
M PLASTICITY flat
M PLASTICITY_UPDATES 0
M PLASTICITY_LAST_GAIN_Q 4096
M SPA_ENABLED 1
M SPA_ALPHA_Q 3482
M SPA_STRENGTH_Q 1229
M SPA_HISTORY_MAX 8
M SPA_HISTORY_N 1
M SPA_SENTENCES 1
M SPA_LAST_CONN_Q 0
M SPA_LAST_TEMP_Q 4096
R 0
S 0 8 1 2 3 4 5 6 7 $(touch /tmp/bashformer-spa-should-never-run)
C 0
Z
BAD
rm -f /tmp/bashformer-spa-should-never-run
for runtime in "$B" "$C"; do
    if "$runtime" --weights "$W" --prompt A --next-id --soma "$TMP/bad.soma" >/dev/null 2>&1; then
        printf 'accepted injectable BFSOMA3: %s\n' "$runtime" >&2
        exit 1
    fi
done
[[ ! -e /tmp/bashformer-spa-should-never-run ]]

# Bounds fail closed.
invalid=(
    '--plasticity other'
    '--spa-alpha -0.01'
    '--spa-alpha 1.01'
    '--spa-strength -0.01'
    '--spa-strength 1.01'
    '--spa-history 0'
    '--spa-history 9'
)
for spec in "${invalid[@]}"; do
    read -r -a args <<< "$spec"
    if "$B" --weights "$W" --prompt A --next-id "${args[@]}" >/dev/null 2>&1; then
        printf 'accepted invalid SPA/plasticity args: %s\n' "$spec" >&2
        exit 1
    fi
done

printf 'spa: sentence embeddings, connectedness modulation, debt-gated plasticity, BFSOMA3 migration/persistence, parser safety, and exact C parity pass\n'
