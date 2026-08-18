# Bashformer v0 architecture

## One graph, two numerical bodies

The forge and runtime implement the same causal decoder graph:

1. `src/train.c` builds the float training graph through installed upstream
   Notorch.
2. `bashformer.sh` executes an exported fixed-point graph with GNU Bash
   builtins.
3. `tools/reference.c` is an independent integer oracle used only by tests.

The boundary is BFW1. There is no FFI, subprocess, or shared-library call from
Bash during inference.

## Geometry

```text
V = 96        newline, space, printable ASCII
T = 64        maximum context
D = 32        model width
L = 2         decoder blocks
Hq = 4        query heads
Hkv = 1       key/value heads
Dh = 8        head width
F = 64        SwiGLU width
RoPE theta = 500000
```

The tied-head parameter count is 20,640:

```text
WTE + RMSF + L * (
  RMS1 + WQ + WK + WV + WO + RMS2 + WG + WU + WD
)
```

## Decoder block

For each new token and layer:

```text
x
├─ RMSNorm
├─ Wq -> adjacent-pair RoPE ──────────────┐
├─ Wk -> adjacent-pair RoPE -> K cache ───┼─ GQA causal attention -> Wo -> residual
└─ Wv                       -> V cache ────┘

x
├─ RMSNorm
├─ Wgate -> SiLU ─┐
├─ Wup ───────────┼─ multiply -> Wdown -> residual
└─────────────────┘
```

Final RMSNorm feeds the token-embedding matrix transposed as the language-model
head.

## Why GQA matters in Bash

Four query heads share one KV head. Compared with four-head MHA, WK, WV, and both
caches are four times smaller. This removes nested-loop iterations and array
traffic from the shell hot path.

## Incremental decode

The runtime performs one-token forward passes. Each layer keeps flat K/V arrays
with logical shape `[context, kv_heads * head_dim]`. Position `p` attends only to
positions `0..p`. v0 stops at the context boundary rather than silently changing
positional semantics with a sliding window.

## Fixed-point contract

Weights, activations, RoPE values, LUT values, field coefficients, and dynamic
state are signed Q12 integers:

```text
Q = 1 << 12 = 4096
real(x) ≈ integer(x) / Q
```

The exporter deliberately uses one global scale. Per-tensor scales may be added
only after float-to-fixed traces justify the extra state.

### Projection

```text
sum = Σ weight[i] * input[i]       # Q24 accumulator
out = round_signed(sum / Q)        # Q12
```

### RMSNorm

```text
mean_q24 = Σ x[i]^2 / D + eps_q24
rms_q12  = isqrt(mean_q24)
out[i]    = round_signed((x[i] * gamma[i]) / rms_q12)
```

The square root is integer Newton iteration.

### RoPE

The exporter writes Q12 sine/cosine tables for every position and lane pair.
Llama-style adjacent pairs `(0,1), (2,3), ...` rotate independently in each
head. Bash performs only multiplication, addition, and signed Q shifting.

### Attention

Q/K scores use exported `1/sqrt(head_dim)` in Q12. After subtracting the row
maximum, an exported `exp(-x)` table covers `[0,16]` at `1/64` increments.
Attention probabilities are not separately rounded:

```text
out[d] = Σ exp_score[t] * value[t,d] / Σ exp_score[t]
```

### SwiGLU

An exported sigmoid table covers `[-8,8]` at `1/64` increments:

```text
silu(g) = g * sigmoid(g)
out     = silu(gate) * up
```

## Field around the graph

The immutable Transformer ends at tied-head logits. v0.4 then wraps it in a
stateful Q12 field:

```text
raw logits
  -> Hebbian H
  -> DESTINY × PROPHECY
  -> PAIN
  -> FOCUS/SPREAD
  -> ENTROPY_FLOOR
  -> RESONANCE_CEILING
  -> token selection at base_temperature × velocity
  -> prophecy debt
  -> debt decay
  -> recovery gate
  -> Hebbian ingest
```

The field never changes WTE, layer weights, RMS weights, RoPE tables, or KV
cache. Its state lives separately in BFSOMA2.

## Three memory timescales

```text
BFW1 weights     long-term trained neural body
KV cache         within-context transient state
BFSOMA2          cross-process experiential and dynamic field state
```

BFSOMA2 contains Hebbian co-occurrence, recent-token ring, debt, decay, laws,
velocity, field-step count, and recovery count.

## Closed-loop timing

At token `t`:

1. use the current velocity to compute effective temperature;
2. choose token `y_t` from the final field logits;
3. compute debt from `y_t` against the same logits;
4. add and decay debt;
5. possibly force velocity to `nomove`;
6. ingest `y_t` into H memory;
7. decode `y_t`; token `t+1` sees the updated field.

This ordering makes recovery causal. A recovery trace that changes only the saved
state but not the next sampling temperature is considered a failed implementation.

## Parity gates

`make test` refuses to treat plausible text as correctness. It checks:

- vanilla stage checksums and sampled tokens;
- each stateless field operator;
- every velocity mode and RNG transition;
- H-term updates and serialized soma;
- both laws separately and together;
- non-max debt accumulation;
- threshold recovery and next-token temperature;
- BFSOMA1 migration to BFSOMA2;
- read-only and field-off non-mutation;
- malformed model/state rejection;
- empty-PATH inference;
- bootstrap/backend lifecycle;
- forge/checkpoint/export control flow;
- installed-Notorch doctor ABI.

The same tests run with GCC and Clang. The C oracle and doctor also pass ASan and
UBSan in release validation.

The next numerical acceptance gate remains an actual trained upstream-Notorch
checkpoint compared with its exported Q12 body at layer boundaries and top-k
output. Quantization may move logits; unexplained graph disagreement may not.
