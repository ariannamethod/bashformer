#!/usr/bin/env bash
# Stateful Method dynamics: laws, prophecy debt, recovery, and BFSOMA3 persistence.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
B="$ROOT/bashformer.sh"
C="$ROOT/build/reference"
W="$ROOT/weights/fixture.bfw"
TMP=${TMPDIR:-/tmp}/bashformer-dynamics.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

compare_run() {
    local name=$1; shift
    "$B" --weights "$W" "$@" >"$TMP/$name.b.out" 2>"$TMP/$name.b.trace"
    "$C" --weights "$W" "$@" >"$TMP/$name.c.out" 2>"$TMP/$name.c.trace"
    cmp "$TMP/$name.c.out" "$TMP/$name.b.out"
    cmp "$TMP/$name.c.trace" "$TMP/$name.b.trace"
}

# The two laws and their composition are integer-identical to the independent C port.
compare_run floor --prompt AB --trace --next-id --method \
    --entropy-floor 1 --resonance-ceiling 1
compare_run ceiling --prompt AB --trace --next-id --method \
    --entropy-floor 0 --resonance-ceiling 0
compare_run laws --prompt AB --trace --next-id --method \
    --entropy-floor 0.73 --resonance-ceiling 0.41 \
    --destiny 0.35 --prophecy 19 --pain 0.2 --focus 0.8 --spread 0.1

# At least one law must alter the final distribution, not merely parse a flag.
compare_run neutral-laws --prompt AB --trace --next-id --method \
    --entropy-floor 0 --resonance-ceiling 1
if cmp -s "$TMP/floor.b.trace" "$TMP/neutral-laws.b.trace"; then
    printf 'entropy-floor law did not alter the trace\n' >&2
    exit 1
fi

grep -q ' floor=4096 ceiling=4096 ' "$TMP/floor.b.trace"
grep -q ' floor=0 ceiling=0 ' "$TMP/ceiling.b.trace"

# Sweep mixed fixed-point states so rounding/order changes cannot hide behind one fixture.
floors=(0 0.1 0.37 0.73 1)
ceilings=(0 0.23 0.58 0.95 1)
debts=(0 1.25 4.99 6 100)
decays=(0 0.5 0.998 1 0.91)
velocities=(raw walk run breathe nomove)
for ((i=0; i<25; i++)); do
    j=$((i % 5)); k=$(((i * 3 + 1) % 5)); n=$(((i * 2 + 4) % 5))
    compare_run "matrix-$i" --prompt AB --tokens 4 --generated-only --trace \
        --temperature 0.8 --seed $((i + 1)) --method \
        --entropy-floor "${floors[j]}" --resonance-ceiling "${ceilings[k]}" \
        --debt "${debts[n]}" --debt-decay "${decays[k]}" \
        --velocity "${velocities[j]}" --prophecy $((1 + (i * 7) % 64)) \
        --destiny "${floors[k]}" --pain "${floors[n]}" \
        --focus "${ceilings[j]}" --spread "${floors[j]}"
done

# A deterministic sampled trajectory must accrue non-zero prophecy debt exactly.
compare_run sampled-debt --prompt AB --tokens 8 --generated-only \
    --temperature 1 --seed 2 --method --trace
# The fixture/seed pair is pinned because it crosses the non-max path repeatedly.
grep -Eq '^TRACE debt chosen=[0-9]+ add=[1-9][0-9]* ' "$TMP/sampled-debt.b.trace"
grep -Eq '^TRACE debt .* total=[1-9][0-9]* ' "$TMP/sampled-debt.b.trace"

# Recovery is live feedback, not saved decoration: seeded debt > 5 forces NOMOVE,
# and the very next token observes half-temperature.
compare_run recovery --prompt AB --tokens 3 --generated-only \
    --temperature 1 --seed 2 --method --trace \
    --velocity run --debt 6 --debt-decay 0.998
grep -q '^TRACE debt .* velocity=nomove temp=2048 recovered=1 step=1 recoveries=1$' \
    "$TMP/recovery.b.trace"
grep -q '^TRACE debt .* velocity=nomove temp=2048 recovered=0 step=2 recoveries=1$' \
    "$TMP/recovery.b.trace"
"$B" --weights "$W" --prompt AB --tokens 3 --generated-only \
    --temperature 1 --seed 2 --method --velocity run --debt 0 >"$TMP/no-recovery.out"
if cmp -s "$TMP/no-recovery.out" "$TMP/recovery.b.out"; then
    printf 'recovery changed trace state but not the sampled trajectory\n' >&2
    exit 1
fi

# BFSOMA3 persists the closed loop across independent processes, byte-for-byte.
BS="$TMP/bash.soma"; CS="$TMP/c.soma"
"$B" --weights "$W" --prompt AB --tokens 4 --generated-only --temperature 1 --seed 2 \
    --soma "$BS" --velocity run --debt 6 >"$TMP/s1.b.out" 2>"$TMP/s1.b.trace"
"$C" --weights "$W" --prompt AB --tokens 4 --generated-only --temperature 1 --seed 2 \
    --soma "$CS" --velocity run --debt 6 >"$TMP/s1.c.out" 2>"$TMP/s1.c.trace"
cmp "$TMP/s1.c.out" "$TMP/s1.b.out"
cmp "$TMP/s1.c.trace" "$TMP/s1.b.trace"
cmp "$CS" "$BS"
grep -q '^BFSOMA3$' "$BS"
grep -q '^M VELOCITY nomove$' "$BS"
grep -q '^M RECOVERIES 1$' "$BS"
grep -q '^M FIELD_STEPS 4$' "$BS"
grep -Eq '^M DEBT_Q [1-9][0-9]*$' "$BS"

# A second process loads the cooled body and continues the same state trajectory.
"$B" --weights "$W" --prompt C --tokens 3 --generated-only --temperature 1 --seed 7 \
    --soma "$BS" --trace >"$TMP/s2.b.out" 2>"$TMP/s2.b.trace"
"$C" --weights "$W" --prompt C --tokens 3 --generated-only --temperature 1 --seed 7 \
    --soma "$CS" --trace >"$TMP/s2.c.out" 2>"$TMP/s2.c.trace"
cmp "$TMP/s2.c.out" "$TMP/s2.b.out"
cmp "$TMP/s2.c.trace" "$TMP/s2.b.trace"
cmp "$CS" "$BS"
grep -q 'velocity=nomove' "$TMP/s2.b.trace"
grep -q '^M FIELD_STEPS 7$' "$BS"

# A valid v1 memory is migrated to BFSOMA3 with neutral/default field state.
cat >"$TMP/v1.b.soma" <<'SOMA1'
BFSOMA1
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 2
E 1 2 4096
E 2 1 4096
R 2 1 2
Z
SOMA1
cp "$TMP/v1.b.soma" "$TMP/v1.c.soma"
"$B" --weights "$W" --prompt A --tokens 1 --generated-only --temperature 0 \
    --soma "$TMP/v1.b.soma" >"$TMP/v1.b.out"
"$C" --weights "$W" --prompt A --tokens 1 --generated-only --temperature 0 \
    --soma "$TMP/v1.c.soma" >"$TMP/v1.c.out"
cmp "$TMP/v1.c.out" "$TMP/v1.b.out"
cmp "$TMP/v1.c.soma" "$TMP/v1.b.soma"
grep -q '^BFSOMA3$' "$TMP/v1.b.soma"
grep -q '^M DEBT_DECAY_Q 4088$' "$TMP/v1.b.soma"
grep -q '^M ENTROPY_FLOOR_Q 410$' "$TMP/v1.b.soma"
grep -q '^M RESONANCE_CEILING_Q 3891$' "$TMP/v1.b.soma"

# Read-only and FIELD OFF runs cannot mutate either memory or dynamics.
cp "$BS" "$TMP/read-only.soma"; cp "$BS" "$TMP/read-only.before"
"$B" --weights "$W" --prompt A --tokens 4 --generated-only --temperature 1 \
    --soma "$TMP/read-only.soma" --learn off >/dev/null
cmp "$TMP/read-only.before" "$TMP/read-only.soma"

cp "$BS" "$TMP/field-off.soma"; cp "$BS" "$TMP/field-off.before"
"$B" --weights "$W" --prompt A --tokens 4 --generated-only --temperature 1 \
    --soma "$TMP/field-off.soma" --field off >/dev/null
cmp "$TMP/field-off.before" "$TMP/field-off.soma"

# Parsing and state evolution remain Bash-builtins-only with an empty PATH.
cp "$BS" "$TMP/pathless.soma"
PATH=/definitely-empty "$BASH" "$B" --weights "$W" --prompt A --tokens 2 \
    --generated-only --temperature 1 --soma "$TMP/pathless.soma" --learn off >/dev/null
cmp "$BS" "$TMP/pathless.soma"

# BFSOMA2 is data, never shell. Malformed/injectable state fails closed.
cat >"$TMP/bad.soma" <<'BAD'
BFSOMA2
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 0
M DEBT_Q $(touch /tmp/bashformer-dynamics-should-never-run)
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
R 0
Z
BAD
rm -f /tmp/bashformer-dynamics-should-never-run
if "$B" --weights "$W" --prompt A --next-id --soma "$TMP/bad.soma" >/dev/null 2>&1; then
    printf 'accepted malformed/injectable BFSOMA2\n' >&2
    exit 1
fi
[[ ! -e /tmp/bashformer-dynamics-should-never-run ]]

# Edge counts use the full declared Q12 cap, not an accidental signed-32 parser limit.
cat >"$TMP/wide-edge.b.soma" <<'WIDE'
BFSOMA2
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 1
M DEBT_Q 0
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
E 1 2 3000000000
R 1 1
Z
WIDE
cp "$TMP/wide-edge.b.soma" "$TMP/wide-edge.c.soma"
"$B" --weights "$W" --prompt A --trace --next-id --soma "$TMP/wide-edge.b.soma" --learn off \
    >"$TMP/wide.b.out" 2>"$TMP/wide.b.trace"
"$C" --weights "$W" --prompt A --trace --next-id --soma "$TMP/wide-edge.c.soma" --learn off \
    >"$TMP/wide.c.out" 2>"$TMP/wide.c.trace"
cmp "$TMP/wide.c.out" "$TMP/wide.b.out"
cmp "$TMP/wide.c.trace" "$TMP/wide.b.trace"

# Duplicate field metadata and a missing ring are structural corruption.
cat >"$TMP/duplicate.soma" <<'DUP'
BFSOMA2
M VOCAB 8
M QSHIFT 12
M COOC_TOTAL 0
M DEBT_Q 0
M DEBT_Q 1
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 0
M RECOVERIES 0
R 0
Z
DUP
for runtime in "$B" "$C"; do
    if "$runtime" --weights "$W" --prompt A --next-id --soma "$TMP/duplicate.soma" >/dev/null 2>&1; then
        printf 'accepted duplicate BFSOMA2 metadata: %s\n' "$runtime" >&2
        exit 1
    fi
done

cat >"$TMP/no-ring.soma" <<'NORING'
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
Z
NORING
for runtime in "$B" "$C"; do
    if "$runtime" --weights "$W" --prompt A --next-id --soma "$TMP/no-ring.soma" >/dev/null 2>&1; then
        printf 'accepted BFSOMA2 without ring: %s\n' "$runtime" >&2
        exit 1
    fi
done

# Public law/dynamics bounds are fail-closed.
invalid=(
    '--entropy-floor -0.01'
    '--entropy-floor 1.01'
    '--resonance-ceiling -0.01'
    '--resonance-ceiling 1.01'
    '--debt -0.01'
    '--debt 100.01'
    '--debt-decay -0.01'
    '--debt-decay 1.01'
)
for spec in "${invalid[@]}"; do
    read -r -a args <<< "$spec"
    if "$B" --weights "$W" --prompt A --next-id "${args[@]}" >/dev/null 2>&1; then
        printf 'accepted invalid dynamics arguments: %s\n' "$spec" >&2
        exit 1
    fi
done

printf 'dynamics: laws, sampled debt, live recovery, BFSOMA3 persistence/migration, state gates, parser safety, and exact C parity pass\n'
