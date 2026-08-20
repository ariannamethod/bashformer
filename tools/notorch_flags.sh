#!/usr/bin/env bash
set -euo pipefail
PREFIX=${BASHFORMER_PREFIX:-${PREFIX:-$HOME/.local}}
ENV_FILE="$PREFIX/share/bashformer/notorch.env"
[[ -r $ENV_FILE ]] || { printf 'run ./bootstrap.sh first\n' >&2; exit 1; }
# shellcheck disable=SC1090
source "$ENV_FILE"
case ${1:-} in
    cflags) printf '%s\n' "-I$PREFIX/include/ariannamethod" ;;
    libs)
        printf '%s' "-L$PREFIX/lib -lnotorch -lm -lpthread"
        [[ -z ${BACKEND_LINK:-} ]] || printf ' %s' "$BACKEND_LINK"
        printf '\n'
        ;;
    backend) printf '%s\n' "${BACKEND:-unknown}" ;;
    ref) printf '%s\n' "${NOTORCH_REF:-unknown}" ;;
    *) printf 'usage: %s {cflags|libs|backend|ref}\n' "$0" >&2; exit 2 ;;
esac
