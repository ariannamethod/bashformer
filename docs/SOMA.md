# BFSOMA2 — persistent Bashformer field state

BFSOMA2 is a strict, line-oriented, non-executable state format. It persists the
field that surrounds the immutable BFW1 Transformer body.

The parser never calls `source`, `eval`, `awk`, `sed`, Python, or a C helper.
Every record is checked with Bash builtins. Unknown records, duplicate edges,
invalid ranges, missing metadata, data after `Z`, and shell-looking values are
rejected.

## Example

```text
BFSOMA2
M VOCAB 96
M QSHIFT 12
M COOC_TOTAL 14
M DEBT_Q 23367
M DEBT_DECAY_Q 4088
M ENTROPY_FLOOR_Q 410
M RESONANCE_CEILING_Q 3891
M VELOCITY nomove
M FIELD_STEPS 14
M RECOVERIES 1
E 52 40 4096
E 40 52 4096
R 8 33 44 52 40 51 39 42 40
Z
```

## Records

```text
BFSOMA2                    exact header
M VOCAB n                  model-vocabulary guard
M QSHIFT n                 fixed-point ABI guard
M COOC_TOTAL n             observed-token counter
M DEBT_Q n                 prophecy debt in Q12, 0..100Q
M DEBT_DECAY_Q n           per-token decay in Q12, 0..Q
M ENTROPY_FLOOR_Q n        law coefficient in Q12, 0..Q
M RESONANCE_CEILING_Q n    law coefficient in Q12, 0..Q
M VELOCITY mode            raw|nomove|walk|run|breathe
M FIELD_STEPS n            emitted-token field steps
M RECOVERIES n             RUN/WALK/etc. -> NOMOVE events
E src dst count            sparse directed Q12 co-occurrence edge
R n ids...                 recent-token ring, at most 8 IDs
Z                           mandatory end marker
```

Every BFSOMA2 field metadata record is mandatory. This prevents a partially
written or hand-edited state from silently inheriting process defaults.

## What persists

BFSOMA2 stores two kinds of memory:

1. **Experiential memory** — sparse co-occurrence edges and recent-token ring.
2. **Dynamic field state** — debt, decay, laws, current velocity, field steps,
   and recovery count.

The model weights, KV cache, base sampling temperature, prompt, DESTINY, PAIN,
FOCUS/SPREAD, and RNG state are not stored. Those remain model/session inputs.

## BFSOMA1 migration

The loader accepts the v0.3 format:

```text
BFSOMA1
M VOCAB 96
M QSHIFT 12
M COOC_TOTAL 14
E 52 40 4096
R 2 52 40
Z
```

A v1 load receives v0.4 defaults:

```text
debt                 0
debt_decay           0.998
entropy_floor         0.1
resonance_ceiling     0.95
velocity              raw
field_steps           0
recoveries            0
```

The next writable save emits BFSOMA2. Migration is tested through both Bash and
the independent C oracle, including byte-identical output.

## Learning rule

For each new token, symmetric edges are added to the previous five tokens:

```text
edge(new, previous[d]) += 1/d
edge(previous[d], new) += 1/d
```

The last eight tokens form the H-term context ring. Newer ring positions receive
stronger weight:

```text
H[i] = sum_c cooc[ring[c], i] / (ring_n - c)
H    = H / max(H)
logits[i] += 2 * H[i]
```

Counts are Q12 and capped at `1,000,000 * Q` (4,096,000,000 for Q12). The
state parser therefore accepts the declared count domain rather than truncating
it to signed 32-bit. The overlay runs before DESTINY, PAIN, attention
focus/spread, and laws.

## Mutation rules

A normal `--soma FILE` session loads, applies, learns, advances dynamics, and
writes the file after generation.

Read/apply without mutation:

```bash
./bashformer.sh --weights weights/bashformer.bfw \
  --prompt 'Mina ' --tokens 32 \
  --soma mina.soma --learn off
```

Hard ablation:

```bash
./bashformer.sh --weights weights/bashformer.bfw \
  --prompt 'Mina ' --tokens 32 \
  --soma mina.soma --field off
```

Both modes leave the file byte-identical. `--field off` additionally prevents
all field effects on generation.

## Failure behavior

BFSOMA2 fails closed on, among other things:

- wrong `VOCAB` or `QSHIFT`;
- duplicate headers, metadata, rings, or edges;
- negative/out-of-range debt, laws, IDs, counts, or counters;
- missing required field state;
- unknown velocity;
- missing `Z`;
- any non-comment data after `Z`;
- values such as `$(command)` or other shell-looking payloads.
