# BFSOMA3 — persistent Bashformer field state

BFSOMA3 is a strict, line-oriented, non-executable state format. BFW1 contains
the immutable trained Transformer body; BFSOMA3 contains the field that has
lived around that body.

The runtime never `source`s or `eval`s a soma. Every record is parsed with Bash
builtins and range-checked. Unknown records, duplicate metadata/edges, malformed
sentence vectors, missing end markers, data after `Z`, and shell-looking values
fail closed.

## Example

```text
BFSOMA3
M VOCAB 96
M QSHIFT 12
M COOC_TOTAL 38
M DEBT_Q 12168
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY raw
M FIELD_STEPS 5
M RECOVERIES 0
M PLASTICITY debt
M PLASTICITY_UPDATES 20
M PLASTICITY_LAST_GAIN_Q 5623
M SPA_ENABLED 1
M SPA_ALPHA_Q 3482
M SPA_STRENGTH_Q 1229
M SPA_HISTORY_MAX 8
M SPA_HISTORY_N 1
M SPA_SENTENCES 1
M SPA_LAST_CONN_Q 4096
M SPA_LAST_TEMP_Q 2867
E 52 40 6144
R 8 33 44 52 40 51 39 42 40
S 0 32 120 -44 ... 73
C 5 52 40 51 39 42
Z
```

## Records

```text
BFSOMA3                         exact header
M VOCAB n                       model-vocabulary guard
M QSHIFT n                      fixed-point ABI guard
M COOC_TOTAL n                  observed-token count
M DEBT_Q n                      prophecy debt, 0..100Q
M DEBT_DECAY_Q n                per-token decay, 0..Q
M ENTROPY_FLOOR_Q n             law coefficient, 0..Q
M RESONANCE_CEILING_Q n         law coefficient, 0..Q
M VELOCITY mode                 raw|nomove|walk|run|breathe
M FIELD_STEPS n                 emitted-token field steps
M RECOVERIES n                  forced NOMOVE events
M PLASTICITY flat|debt          Hebbian learning law
M PLASTICITY_UPDATES n          debt-gated update count
M PLASTICITY_LAST_GAIN_Q n      last gain, Q..2Q
M SPA_ENABLED 0|1               sentence field switch
M SPA_ALPHA_Q n                 embedding recency, 0..Q
M SPA_STRENGTH_Q n              logit sharpening, 0..Q
M SPA_HISTORY_MAX n             retained sentences, 1..8
M SPA_HISTORY_N n               live sentence-vector count
M SPA_SENTENCES n               committed-sentence count
M SPA_LAST_CONN_Q n             latest connectedness, 0..Q
M SPA_LAST_TEMP_Q n             latest SPA temperature, 1..Q
E src dst count                 sparse directed Q12 co-occurrence edge
R n ids...                      recent-token ring, max 8
S index dim values...           one Q12 sentence embedding
C n ids...                      unfinished sentence, max CTX
Z                                mandatory end marker
```

All v3 metadata is mandatory. `S` records must be contiguous from index zero,
match the model dimension, and agree with `SPA_HISTORY_N`.

## Three persistent layers

1. **Token circulation** — sparse co-occurrence edges and the eight-token ring.
2. **Dynamic field** — debt, laws, velocity, steps, and recovery count.
3. **Sentence resonance** — sentence embeddings, unfinished sentence,
   connectedness/temperature telemetry, and SPA configuration.

KV cache, prompt, RNG state, and BFW1 weights remain session/model state and are
not serialized here.

## Sentence memory

At `.`, `!`, `?`, or newline, the completed sentence becomes an exponentially
weighted mean of token embeddings. The newest token has weight 1; older tokens
receive successive powers of `SPA_ALPHA`. Up to `SPA_HISTORY_MAX` vectors are
kept; overflow drops the oldest.

The unfinished sentence is preserved as token IDs so a new process can continue
forming the same embedding rather than beginning with an artificial blank.

## Learning and neuromodulation

Flat Hebbian learning preserves the earlier law:

```text
edge(new, previous[d]) += 1/d
edge(previous[d], new) += 1/d
```

Debt-gated mode multiplies each increment by:

```text
gain = 1 + debt / (debt + 5)       # [1, 2)
```

The emitted token first creates prophecy debt, then its co-occurrence edges are
written. Surprise therefore modulates learning in the same causal step.

## Migration

The loader accepts BFSOMA1 and BFSOMA2. Missing v3 state receives neutral
values:

```text
plasticity        flat
SPA               off
SPA alpha         0.85
SPA strength      0.30
SPA history       empty
unfinished        empty
```

The next writable save emits BFSOMA3. Bash and the independent C oracle must
serialize byte-identical migration output.

## Mutation gates

`--learn off` may read and apply existing H/SPA memory but leaves the file
byte-identical. `--field off` additionally disables all overlays and all state
mutation. A run with an empty `PATH` still parses and applies BFSOMA3 because the
production path uses Bash builtins only.
