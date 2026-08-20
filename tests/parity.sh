#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
W="$ROOT/weights/fixture.bfw"
B="$ROOT/bashformer.sh"
C="$ROOT/build/reference"
TMP=${TMPDIR:-/tmp}/bashformer-parity.$$
mkdir -p "$TMP"
trap 'rm -rf -- "$TMP"' EXIT

[[ $("$B" --version) == 'bashformer 0.5.0' ]]

prompts=('' 'A' 'AB' 'FACE' 'A B' $'A\nB')
for prompt in "${prompts[@]}"; do
    "$B" --weights "$W" --prompt "$prompt" --trace --next-id >"$TMP/b.out" 2>"$TMP/b.trace"
    "$C" --weights "$W" --prompt "$prompt" --trace --next-id >"$TMP/c.out" 2>"$TMP/c.trace"
    cmp "$TMP/c.out" "$TMP/b.out"
    cmp "$TMP/c.trace" "$TMP/b.trace"
done

expected_builtin=$("$C" --weights "$W" --prompt AB --next-id)
builtin_only=$(PATH=/definitely-empty "$BASH" "$B" --weights "$W" --prompt AB --next-id)
[[ $builtin_only == "$expected_builtin" ]]

bgen=$("$B" --weights "$W" --prompt AB --tokens 6 --generated-only)
cgen=$("$C" --weights "$W" --prompt AB --tokens 6)
# C echoes the prompt; compare its generated suffix.
[[ $cgen == AB* ]]
[[ $bgen == "${cgen:2}" ]]


# Repeat once at the real v0 geometry (D=32, 4Q/1KV, FFN=64, V=96).
WP="$ROOT/weights/fixture-prod.bfw"
PROMPT='The blood was '
"$B" --weights "$WP" --prompt "$PROMPT" --trace --next-id >"$TMP/bp.out" 2>"$TMP/bp.trace"
"$C" --weights "$WP" --prompt "$PROMPT" --trace --next-id >"$TMP/cp.out" 2>"$TMP/cp.trace"
cmp "$TMP/cp.out" "$TMP/bp.out"
cmp "$TMP/cp.trace" "$TMP/bp.trace"

# The text format is data, not shell source. Structural corruption must be
# rejected before any model arithmetic begins.
while IFS= read -r line; do [[ $line == Z ]] || printf '%s\n' "$line"; done < "$W" > "$TMP/no-z.bfw"
if "$B" --weights "$TMP/no-z.bfw" --prompt A --next-id >/dev/null 2>&1; then
    printf 'parser accepted a model without the Z end marker\n' >&2
    exit 1
fi

{
    printf 'BFW1\nM QSHIFT 12\nM QSHIFT 12\n'
    while IFS= read -r line; do
        [[ $line == BFW1 || $line == 'M QSHIFT 12' ]] || printf '%s\n' "$line"
    done < "$W"
} > "$TMP/duplicate-meta.bfw"
if "$B" --weights "$TMP/duplicate-meta.bfw" --prompt A --next-id >/dev/null 2>&1; then
    printf 'parser accepted duplicate metadata\n' >&2
    exit 1
fi

# A value can clear the load-time bound and still blow the 64-bit budget once it
# is an activation: matvec squares it against the weights and attention squares
# it again. Both engines must refuse such a model, and refuse it the same way -
# if only one guards, parity is comparing a runtime against a broken oracle.
in_wte=0
touched=0
while IFS= read -r line; do
    if (( in_wte )) && [[ $line == D* ]]; then
        read -r _ values <<< "$line"
        out=D
        for _v in $values; do out+=" 67108864"; done
        printf '%s\n' "$out"
        touched=1
        continue
    fi
    [[ $line == "T WTE"* ]] && in_wte=1
    [[ $line == E ]] && in_wte=0
    printf '%s\n' "$line"
done < "$WP" > "$TMP/overflow.bfw"
(( touched )) || { printf 'could not build the overflow fixture\n' >&2; exit 1; }

if "$B" --weights "$TMP/overflow.bfw" --prompt A --next-id >/dev/null 2>"$TMP/ovf.b"; then
    printf 'bash executed a model that overflows the integer contract\n' >&2
    exit 1
fi
if "$C" --weights "$TMP/overflow.bfw" --prompt A --next-id >/dev/null 2>"$TMP/ovf.c"; then
    printf 'the C oracle executed a model that overflows the integer contract\n' >&2
    exit 1
fi
b_msg=$(<"$TMP/ovf.b")
c_msg=$(<"$TMP/ovf.c")
[[ $b_msg == *"exceeds the 64-bit fixed-point contract"* ]]
[[ ${b_msg#bashformer: } == ${c_msg#reference: } ]]

printf 'parity: exact stage checksums and greedy tokens agree (%d small prompts + production geometry); parser guards pass\n' "${#prompts[@]}"
