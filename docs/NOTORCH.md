# Notorch as an installed dependency

Bashformer does not vendor Notorch. The forge consumes one pinned upstream commit from
`requirements.txt`, installs it under a prefix, then treats the installed library as the
build dependency.

## One command

```bash
make bootstrap
make doctor
```

Default prefix:

```text
${BASHFORMER_PREFIX:-$HOME/.local}
```

Installed contract:

```text
$PREFIX/lib/libnotorch.a
$PREFIX/include/ariannamethod/notorch.h
$PREFIX/include/ariannamethod/gguf.h
$PREFIX/share/bashformer/notorch.commit
$PREFIX/share/bashformer/notorch.env
```

The checkout used for a network install is temporary and deleted after `make install`.
No Notorch source enters the Bashformer tree.

## Backend selection

`NOTORCH_BACKEND=auto` chooses the first usable backend in this order:

```text
macOS                     -> Accelerate
Linux/other + OpenBLAS    -> OpenBLAS
x86_64 + AVX2 + FMA       -> Notorch in-house SIMD
otherwise                 -> scalar C
```

Force one explicitly:

```bash
NOTORCH_BACKEND=openblas make bootstrap
NOTORCH_BACKEND=simd     make bootstrap
NOTORCH_BACKEND=scalar   make bootstrap
```

Supported values are `auto`, `accelerate`, `openblas`, `simd`, and `scalar`.
The SIMD path is Notorch's own AVX2+FMA/pthread CBLAS shim and requires no external
math package. `tools/notorch_flags.sh` records enough information for consumers to link
against the installed static archive:

```bash
./tools/notorch_flags.sh cflags
./tools/notorch_flags.sh libs
./tools/notorch_flags.sh backend
./tools/notorch_flags.sh ref
```

## Air-gapped / already-cloned upstream

A local checkout is supported without copying it into this repository:

```bash
NOTORCH_SOURCE=/path/to/notorch make bootstrap
```

If it is a Git checkout, HEAD must equal the pinned commit. A mismatch fails closed.
`NOTORCH_ALLOW_UNPINNED=1` exists for deliberate development only.

## Doctor

`make doctor` links a tiny real transformer step against the installed library. It
exercises the exact API Bashformer's forge needs:

- tensor allocation and Xavier init;
- tape recording and backward;
- RMSNorm;
- GQA;
- explicit RoPE theta 500000;
- SwiGLU composition (`SiLU * up`);
- tied output projection;
- sequence cross entropy;
- gradient clipping;
- Chuck optimizer.

If this probe fails, Bashformer refuses to call the installation forge-compatible.

## Upstream acceptance

On a machine that can access the pinned source (or has `NOTORCH_SOURCE`):

```bash
make bootstrap
make doctor
make upstream-smoke
```

`upstream-smoke` trains the real Bashformer graph briefly on `data/tiny.txt`, exports
BFW1, and immediately executes the resulting weights in pure Bash. It is a wiring test,
not a quality benchmark.

## What the bootstrap actually asks upstream to build

Bashformer does not invent a private Notorch build system. It drives the
upstream `make install` target with backend variables and a user prefix. The
installed archive contains the same tensor/autograd/transformer implementation
used by the upstream examples.

Conceptually, the upstream choices are:

```text
make / make lib        Accelerate on macOS or OpenBLAS on Linux
make simd              in-house AVX2+FMA + pthread CBLAS shim
make cpu               scalar portable fallback
make gpu               CUDA/cuBLAS build
make install PREFIX=…  static library + public headers
```

Bashformer selects one CPU training backend, records it in
`$PREFIX/share/bashformer/notorch.env`, and links `src/train.c` through
`tools/notorch_flags.sh`. Notorch never appears in the pure-Bash inference
process.
