# BFW1 — Bashformer Weight Format

BFW1 is deliberately boring ASCII. The training/export path writes it; GNU Bash
loads it with `read` and indexed arrays. Runtime needs no `od`, `awk`, `bc`, Python,
or shared library.

```text
BFW1
M QSHIFT 12
M VOCAB 96
...
V 0a202122...7e
T WTE 3072
D 12 -8 ...
D ...
E
...
Z
```

Records:

- `M NAME INTEGER` — scalar metadata.
- `V HEX` — exactly `2 * VOCAB` hexadecimal characters; one byte per token.
- `T NAME COUNT` — begin a flat row-major tensor.
- `D ...` — signed decimal tensor values.
- `E` — end the current tensor. Exactly `COUNT` values must have appeared.
- `Z` — end of file.

All weights, activations, RoPE values, and sigmoid values use signed Q-format with
`scale = 1 << QSHIFT`. `EXP_NEG` also uses that scale. Tensor shapes are implied
by metadata and tensor names, then checked by both the Bash and C runtimes.

The v0 forge exports Llama-style adjacent-pair RoPE at theta 500,000. The table
is part of the model artifact, so the Bash runtime never calls a transcendental
function and does not need to know how the values were produced.

The first architecture is a one-or-more-layer decoder with RMSNorm, GQA, RoPE,
SwiGLU, residual connections, and a byte-level output vocabulary. `TIE_HEAD=1`
means the language-model head reuses `WTE` as `[VOCAB, DIM]`.
