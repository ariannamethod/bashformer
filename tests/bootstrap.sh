#!/usr/bin/env bash
# Exercise the dependency installer without network access.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TMP=${TMPDIR:-/tmp}/bashformer-bootstrap.$$
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$TMP/upstream" "$TMP/home" "$TMP/cache"

cat > "$TMP/upstream/Makefile" <<'MAKE'
.PHONY: install
install:
	mkdir -p "$(PREFIX)/lib" "$(PREFIX)/include/ariannamethod"
	printf 'fake archive\n' > "$(PREFIX)/lib/libnotorch.a"
	printf '/* fake header */\n' > "$(PREFIX)/include/ariannamethod/notorch.h"
	printf '%s|%s|%s\n' "$(BLAS_FLAGS)" "$(BLAS_LIBS)" "$(BLAS_NAME)" > "$(PREFIX)/share-backend.txt"
MAKE

git -C "$TMP/upstream" init -q
git -C "$TMP/upstream" -c user.name=fixture -c user.email=fixture@example.invalid add Makefile
git -C "$TMP/upstream" -c user.name=fixture -c user.email=fixture@example.invalid commit -qm fixture
REF=$(git -C "$TMP/upstream" rev-parse HEAD)
printf 'notorch git %s %s\n' "$TMP/upstream" "$REF" > "$TMP/requirements.txt"

HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/prefix" NOTORCH_BACKEND=scalar \
    "$ROOT/bootstrap.sh" "$TMP/requirements.txt" > "$TMP/first.log"

[[ -f "$TMP/prefix/lib/libnotorch.a" ]]
[[ -f "$TMP/prefix/include/ariannamethod/notorch.h" ]]
[[ $(<"$TMP/prefix/share/bashformer/notorch.commit") == "$REF" ]]

# The checkout is a construction scaffold, not a vendor directory or cache fossil.
shopt -s nullglob
leftovers=("$TMP/cache/bashformer"/notorch.*)
(( ${#leftovers[@]} == 0 ))

HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/prefix" NOTORCH_BACKEND=scalar \
    "$ROOT/bootstrap.sh" "$TMP/requirements.txt" > "$TMP/second.log"
[[ $(<"$TMP/second.log") == *'already installed'* ]]

flags=$(HOME="$TMP/home" BASHFORMER_PREFIX="$TMP/prefix" "$ROOT/tools/notorch_flags.sh" cflags)
[[ $flags == "-I$TMP/prefix/include/ariannamethod" ]]
[[ $(HOME="$TMP/home" BASHFORMER_PREFIX="$TMP/prefix" "$ROOT/tools/notorch_flags.sh" backend) == scalar ]]
[[ $(HOME="$TMP/home" BASHFORMER_PREFIX="$TMP/prefix" "$ROOT/tools/notorch_flags.sh" ref) == "$REF" ]]
[[ $(<"$TMP/prefix/share-backend.txt") == '||scalar' ]]

# Air-gapped consumers can point bootstrap at an existing, pinned upstream checkout.
HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/local-prefix" \
    NOTORCH_SOURCE="$TMP/upstream" NOTORCH_BACKEND=scalar \
    "$ROOT/bootstrap.sh" "$TMP/requirements.txt" > "$TMP/local.log"
[[ -d "$TMP/upstream/.git" ]]
[[ $(<"$TMP/local-prefix/share/bashformer/notorch.commit") == "$REF" ]]

# Pin mismatch is rejected unless explicitly opted out.
printf 'notorch git %s %040d
' "$TMP/upstream" 0 > "$TMP/wrong-requirements.txt"
if HOME="$TMP/home" BASHFORMER_PREFIX="$TMP/wrong-prefix" NOTORCH_SOURCE="$TMP/upstream" \
    NOTORCH_BACKEND=scalar "$ROOT/bootstrap.sh" "$TMP/wrong-requirements.txt" >/dev/null 2>&1; then
    printf 'bootstrap accepted mismatched NOTORCH_SOURCE pin
' >&2
    exit 1
fi

# On AVX2+FMA x86_64, the zero-external-library SIMD path is a first-class install backend.
flags_line=$(grep -m1 -E '^(flags|Features)[[:space:]]*:' /proc/cpuinfo 2>/dev/null || true)
if [[ $(uname -m) == x86_64 && " $flags_line " == *' avx2 '* && " $flags_line " == *' fma '* ]]; then
    HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/simd-prefix" \
        NOTORCH_SOURCE="$TMP/upstream" NOTORCH_BACKEND=simd \
        "$ROOT/bootstrap.sh" "$TMP/requirements.txt" >/dev/null
    [[ $(HOME="$TMP/home" BASHFORMER_PREFIX="$TMP/simd-prefix" "$ROOT/tools/notorch_flags.sh" backend) == simd ]]
    [[ $(<"$TMP/simd-prefix/share-backend.txt") == '-DUSE_SIMD -mavx2 -mfma|-lpthread|in-house SIMD' ]]
fi

# The optional Method dependency follows the same installed-not-vendored rule.
mkdir -p "$TMP/method"
cat > "$TMP/method/Makefile" <<'MAKE'
.PHONY: install
install:
	mkdir -p "$(PREFIX)/lib" "$(PREFIX)/include/ariannamethod" "$(PREFIX)/bin"
	printf 'fake aml archive\n' > "$(PREFIX)/lib/libaml.a"
	printf '/* fake aml header */\n' > "$(PREFIX)/include/ariannamethod/ariannamethod.h"
	printf '#!/bin/sh\n' > "$(PREFIX)/bin/aml"
MAKE
git -C "$TMP/method" init -q
git -C "$TMP/method" -c user.name=fixture -c user.email=fixture@example.invalid add Makefile
git -C "$TMP/method" -c user.name=fixture -c user.email=fixture@example.invalid commit -qm fixture
METHOD_REF=$(git -C "$TMP/method" rev-parse HEAD)
printf 'ariannamethod git %s %s\n' "$TMP/method" "$METHOD_REF" > "$TMP/method-requirements.txt"
HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/method-prefix" \
    "$ROOT/bootstrap.sh" "$TMP/method-requirements.txt" >/dev/null
[[ -f "$TMP/method-prefix/lib/libaml.a" ]]
[[ $(<"$TMP/method-prefix/share/bashformer/ariannamethod.commit") == "$METHOD_REF" ]]
leftovers=("$TMP/cache/bashformer"/ariannamethod.*)
(( ${#leftovers[@]} == 0 ))

# A failed upstream build must also erase its temporary checkout.
mkdir -p "$TMP/failing"
cat > "$TMP/failing/Makefile" <<'MAKE'
.PHONY: install
install:
	false
MAKE
git -C "$TMP/failing" init -q
git -C "$TMP/failing" -c user.name=fixture -c user.email=fixture@example.invalid add Makefile
git -C "$TMP/failing" -c user.name=fixture -c user.email=fixture@example.invalid commit -qm fixture
BAD_REF=$(git -C "$TMP/failing" rev-parse HEAD)
printf 'notorch git %s %s\n' "$TMP/failing" "$BAD_REF" > "$TMP/failing-requirements.txt"
if HOME="$TMP/home" XDG_CACHE_HOME="$TMP/cache" BASHFORMER_PREFIX="$TMP/bad-prefix" \
    "$ROOT/bootstrap.sh" "$TMP/failing-requirements.txt" >/dev/null 2>&1; then
    printf 'bootstrap unexpectedly accepted a failing upstream build\n' >&2
    exit 1
fi
leftovers=("$TMP/cache/bashformer"/notorch.*)
(( ${#leftovers[@]} == 0 ))

printf 'bootstrap: notorch + optional Method install, stamps, reuse, and success/failure cleanup pass\n'
