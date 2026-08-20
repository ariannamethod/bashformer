#!/usr/bin/env bash
# Install upstream build dependencies. Nothing is copied into the repository.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PREFIX=${BASHFORMER_PREFIX:-${PREFIX:-$HOME/.local}}
REQ=${1:-"$ROOT/requirements.txt"}
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bashformer
STAMP_DIR="$PREFIX/share/bashformer"
NOTORCH_BACKEND=${NOTORCH_BACKEND:-auto}
mkdir -p "$CACHE" "$STAMP_DIR" "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/include"

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'bashformer: missing command: %s\n' "$1" >&2
        exit 127
    }
}
need git
need make
need cc

bf_cpu_has_avx2_fma() {
    local machine
    machine=$(uname -m)
    [[ $machine == x86_64 || $machine == amd64 ]] || return 1
    if [[ -r /proc/cpuinfo ]]; then
        local flags
        flags=$(grep -m1 -E '^(flags|Features)[[:space:]]*:' /proc/cpuinfo 2>/dev/null || true)
        [[ " $flags " == *' avx2 '* && " $flags " == *' fma '* ]] || return 1
    elif command -v sysctl >/dev/null 2>&1; then
        local features
        features=$(sysctl -n machdep.cpu.features machdep.cpu.leaf7_features 2>/dev/null || true)
        [[ " $features " == *' AVX2 '* && " $features " == *' FMA '* ]] || return 1
    else
        return 1
    fi
    printf 'int main(void){return 0;}\n' | ${CC:-cc} -x c -mavx2 -mfma -c -o /dev/null - >/dev/null 2>&1
}

bf_openblas_flags() {
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openblas; then
        OPENBLAS_CFLAGS=$(pkg-config --cflags openblas)
        OPENBLAS_LIBS=$(pkg-config --libs openblas)
        return 0
    fi
    local probe="$CACHE/.openblas-probe.$$"
    if printf '#include <cblas.h>\nint main(void){float x=0; cblas_saxpy(1,1,&x,1,&x,1); return 0;}\n' | \
        ${CC:-cc} -x c - -lopenblas -o "$probe" >/dev/null 2>&1; then
        rm -f -- "$probe"
        OPENBLAS_CFLAGS=''
        OPENBLAS_LIBS='-lopenblas'
        return 0
    fi
    rm -f -- "$probe"
    return 1
}

bf_choose_notorch_backend() {
    local requested=${1:-auto} os
    os=$(uname -s)
    case $requested in
        auto)
            if [[ $os == Darwin ]]; then
                REPLY=accelerate
            elif bf_openblas_flags; then
                REPLY=openblas
            elif bf_cpu_has_avx2_fma; then
                REPLY=simd
            else
                REPLY=scalar
            fi
            ;;
        accelerate)
            [[ $os == Darwin ]] || { printf 'notorch: Accelerate backend requires macOS\n' >&2; return 1; }
            REPLY=accelerate
            ;;
        openblas)
            bf_openblas_flags || { printf 'notorch: OpenBLAS headers/library not found\n' >&2; return 1; }
            REPLY=openblas
            ;;
        simd)
            bf_cpu_has_avx2_fma || { printf 'notorch: SIMD backend requires x86_64 AVX2+FMA\n' >&2; return 1; }
            REPLY=simd
            ;;
        scalar) REPLY=scalar ;;
        *) printf 'notorch: unknown backend %s (use auto|accelerate|openblas|simd|scalar)\n' "$requested" >&2; return 1 ;;
    esac
}

bf_checkout_source() {
    local url=$1 ref=$2
    local source_path cleanup=1
    if [[ -n ${NOTORCH_SOURCE:-} ]]; then
        source_path=$NOTORCH_SOURCE
        [[ -d $source_path ]] || { printf 'notorch: NOTORCH_SOURCE is not a directory: %s\n' "$source_path" >&2; return 1; }
        if git -C "$source_path" rev-parse --git-dir >/dev/null 2>&1; then
            local actual
            actual=$(git -C "$source_path" rev-parse HEAD)
            if [[ $actual != "$ref" && ${NOTORCH_ALLOW_UNPINNED:-0} != 1 ]]; then
                printf 'notorch: local source is %s, requirements pin %s\n' "$actual" "$ref" >&2
                printf 'set NOTORCH_ALLOW_UNPINNED=1 only if that mismatch is intentional\n' >&2
                return 1
            fi
        elif [[ ${NOTORCH_ALLOW_UNPINNED:-0} != 1 ]]; then
            printf 'notorch: local source is not a git checkout; cannot verify pin\n' >&2
            return 1
        fi
        cleanup=0
    else
        source_path=$(mktemp -d "$CACHE/notorch.XXXXXXXX")
        git -C "$source_path" init -q
        git -C "$source_path" remote add origin "$url"
        git -C "$source_path" fetch -q --depth 1 origin "$ref"
        git -C "$source_path" checkout -q --detach FETCH_HEAD
    fi
    BF_SOURCE_PATH=$source_path
    BF_SOURCE_CLEANUP=$cleanup
    # The stamp must record what was actually built, not what was requested.
    # With NOTORCH_ALLOW_UNPINNED=1 those two differ, and a stamp that repeats
    # the request would make `make doctor` print a provenance it cannot back.
    BF_SOURCE_REF=$(git -C "$source_path" rev-parse HEAD 2>/dev/null) || BF_SOURCE_REF=$ref
}

install_notorch() {
    local url=$1 ref=$2 stamp="$STAMP_DIR/notorch.commit" env_file="$STAMP_DIR/notorch.env"
    local backend
    bf_choose_notorch_backend "$NOTORCH_BACKEND"
    backend=$REPLY

    if [[ -f $stamp && $(<"$stamp") == "$ref" && -f "$PREFIX/lib/libnotorch.a" && -r $env_file ]]; then
        # shellcheck disable=SC1090
        source "$env_file"
        if [[ ${BACKEND:-} == "$backend" ]]; then
            printf 'notorch %s already installed in %s (%s)\n' "$ref" "$PREFIX" "$backend"
            return
        fi
    fi

    local src cleanup
    bf_checkout_source "$url" "$ref"
    src=$BF_SOURCE_PATH
    cleanup=$BF_SOURCE_CLEANUP
    local -a make_args=("PREFIX=$PREFIX")
    local backend_link=''
    case $backend in
        accelerate)
            make_args+=("BLAS_FLAGS=-DUSE_BLAS -DACCELERATE -DACCELERATE_NEW_LAPACK -framework Accelerate" "BLAS_LIBS=" "BLAS_NAME=Accelerate")
            backend_link='-framework Accelerate'
            ;;
        openblas)
            bf_openblas_flags || { (( cleanup )) && rm -rf -- "$src"; return 1; }
            make_args+=("BLAS_FLAGS=-DUSE_BLAS ${OPENBLAS_CFLAGS:-}" "BLAS_LIBS=${OPENBLAS_LIBS:- -lopenblas}" "BLAS_NAME=OpenBLAS")
            backend_link=${OPENBLAS_LIBS:- -lopenblas}
            ;;
        simd)
            make_args+=("BLAS_FLAGS=-DUSE_SIMD -mavx2 -mfma" "BLAS_LIBS=-lpthread" "BLAS_NAME=in-house SIMD")
            backend_link=''
            ;;
        scalar)
            make_args+=("BLAS_FLAGS=" "BLAS_LIBS=" "BLAS_NAME=scalar")
            backend_link=''
            ;;
    esac

    local rc=0
    make -C "$src" "${make_args[@]}" install || rc=$?
    if (( cleanup )); then rm -rf -- "$src"; fi
    (( rc == 0 )) || return "$rc"

    local built=${BF_SOURCE_REF:-$ref}
    printf '%s\n' "$built" > "$stamp"
    {
        printf 'PREFIX=%q\n' "$PREFIX"
        printf 'BACKEND=%q\n' "$backend"
        printf 'NOTORCH_REF=%q\n' "$built"
        printf 'NOTORCH_PINNED_REF=%q\n' "$ref"
        printf 'BACKEND_LINK=%q\n' "$backend_link"
    } > "$env_file"
    if [[ $built == "$ref" ]]; then
        printf 'installed notorch %s (%s) -> %s\n' "$built" "$backend" "$PREFIX"
    else
        printf 'installed notorch %s UNPINNED (requirements pin %s) (%s) -> %s\n' \
            "$built" "$ref" "$backend" "$PREFIX"
    fi
}

install_ariannamethod() {
    local url=$1 ref=$2 stamp="$STAMP_DIR/ariannamethod.commit"
    if [[ -f $stamp && $(<"$stamp") == "$ref" && -f "$PREFIX/lib/libaml.a" ]]; then
        printf 'ariannamethod.ai %s already installed in %s\n' "$ref" "$PREFIX"
        return
    fi

    local src
    src=$(mktemp -d "$CACHE/ariannamethod.XXXXXXXX")
    local rc=0
    git -C "$src" init -q
    git -C "$src" remote add origin "$url"
    git -C "$src" fetch -q --depth 1 origin "$ref"
    git -C "$src" checkout -q --detach FETCH_HEAD
    make -C "$src" PREFIX="$PREFIX" install || rc=$?
    rm -rf -- "$src"
    (( rc == 0 )) || return "$rc"
    printf '%s\n' "$ref" > "$stamp"
    printf 'installed ariannamethod.ai %s -> %s\n' "$ref" "$PREFIX"
}

while read -r name kind source ref extra; do
    [[ -z ${name:-} || $name == \#* ]] && continue
    [[ -z ${extra:-} ]] || {
        printf 'bad requirements line: %s %s %s %s %s\n' "$name" "$kind" "$source" "$ref" "$extra" >&2
        exit 2
    }
    case "$name:$kind" in
        notorch:git) install_notorch "$source" "$ref" ;;
        ariannamethod:git) install_ariannamethod "$source" "$ref" ;;
        *) printf 'unsupported requirement: %s (%s)\n' "$name" "$kind" >&2; exit 2 ;;
    esac
done < "$REQ"
