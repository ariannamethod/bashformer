# Method field: v0.2–v0.4

Bashformer keeps the Transformer body and the Method field as separate,
measurable layers. The body produces Q12 logits. The field transforms those
logits and maintains its own state. It never rewrites model weights or KV cache.

Production inference implements the field directly in GNU Bash. `ariannamethod.ai`
is an optional source/oracle, not a runtime dependency.

## Pipeline order

For every decoded position:

```text
raw Transformer logits
  1. Hebbian H-term                    optional, stateful
  2. DESTINY × PROPHECY
  3. PAIN
  4. ATTEND_FOCUS / ATTEND_SPREAD
  5. ENTROPY_FLOOR
  6. RESONANCE_CEILING
  7. greedy recomputation
```

Sampling then uses:

```text
base temperature × current VELOCITY
```

After the token is chosen:

```text
  8. compute and register prophecy debt
  9. decay debt
 10. recovery gate may force VELOCITY NOMOVE
 11. ingest the emitted token into Hebbian memory
```

The recovery therefore affects the **next** token. This ordering is tested
against `tools/reference.c` token by token.

## Fixed-point domain

All coefficients and state use the model's Q12 scale:

```text
Q = 4096
real(x) ≈ x / Q
```

The independent C oracle uses the same rounding and operation order. A parity
failure is treated as a bug even when generated text looks plausible.

## DESTINY × PROPHECY

Let `m = max(logits)` and `d_i = m - logits[i]`.

```text
prophecy_scale = clamp(1 + (prophecy - 7) * 0.02, 0.5, 2.0)
destiny_bias   = destiny * prophecy_scale
logits[i]     -= d_i * destiny_bias * 0.5
```

Public ranges:

```text
prophecy  1..64
destiny   0..1
```

## PAIN

PAIN compresses deviations from the mean:

```text
mean       = average(logits)
factor     = 1 - 0.5 * pain
logits[i]  = mean + (logits[i] - mean) * factor
```

Range: `0..1`.

## ATTEND_FOCUS / ATTEND_SPREAD

```text
mean      = average(logits)
scale     = clamp(0.5 + focus - spread, 0.1, 2.0)
logits[i] = mean + (logits[i] - mean) * scale
```

Both coefficients are in `0..1`.

## Entropy floor

The law reads the highest and second-highest logits:

```text
gap       = max_logit - second_max
max_gap   = 10 * (1 - entropy_floor)
```

When `gap > max_gap`, the exact maximum is reduced by half of the excess:

```text
max_logit -= 0.5 * (gap - max_gap)
```

This is a distribution constraint, not an entropy estimator. It prevents a
single path from separating arbitrarily far from the rest.

Default: `0.1`. Range: `0..1`.

## Resonance ceiling

The ceiling uses the same original top-two gap:

```text
ceiling_gap = 10 * resonance_ceiling
```

When `gap > ceiling_gap`, every logit within `0.001` of the original maximum is
reduced by 30% of the excess:

```text
top_logits -= 0.3 * (gap - ceiling_gap)
```

The use of the original gap for both laws is deliberate: it mirrors the source
pipeline rather than silently recomputing a friendlier value after the entropy
pass.

Default: `0.95`. Range: `0..1`.

## Velocity

Velocity multiplies only a positive sampling temperature:

| Mode | Multiplier |
|---|---:|
| `raw` | 1.00 |
| `nomove` | 0.50 |
| `walk` | 0.85 |
| `run` | 1.20 |
| `breathe` | 0.60 |

Greedy decoding (`temperature=0`) is unaffected. Unlike v0.2's one-time
calculation, v0.4 recomputes effective temperature before **every** emitted
token, so a recovery event is causally visible on the next step.

## Prophecy debt

Debt is computed from the final post-field logits and the token that was
actually chosen:

```text
delta = max(logits) - logits[chosen]
```

A peak choice adds zero. A non-peak choice adds:

```text
debt_add = delta / (delta + 1)
```

Then:

```text
debt = min(100, debt + debt_add)
debt = debt * debt_decay
```

Default decay: `0.998` per emitted token. Public ranges:

```text
debt        0..100
debt_decay  0..1
```

## Recovery dynamics

After debt decay:

```text
if debt > 5 and velocity != NOMOVE:
    velocity = NOMOVE
    recoveries += 1
```

The field step counter increments once per emitted token. Debt, velocity,
field-step count, and recovery count can survive process boundaries through
BFSOMA2.

A trace line exposes the loop:

```text
TRACE debt chosen=90 add=3415 total=23367 velocity=nomove \
  temp=3072 recovered=1 step=14 recoveries=1
```

For Q12, `23367 / 4096 ≈ 5.70` debt. With base temperature 1.5,
`NOMOVE` yields `0.75`, represented as 3072.

## Hebbian H-term

The new token forms symmetric, inverse-distance co-occurrence edges with the
previous five tokens:

```text
edge(new, previous[d]) += 1/d
edge(previous[d], new) += 1/d
```

The last eight tokens form a context ring. At logit time:

```text
H[i] = sum_c cooc[ring[c], i] / (ring_n - c)
H    = H / max(H)
logits[i] += 2 * H[i]
```

The H-term is applied before DESTINY. Details are in `SOMA.md`.

## State gates

`--field off` is the hard ablation gate:

- no H-term;
- no stateless logit transformations;
- no debt registration or decay;
- no recovery;
- no co-occurrence learning;
- no state-file write.

It reproduces the exact vanilla Transformer trace.

`--learn off` keeps field application enabled but freezes all mutation:

- existing H memory may influence logits;
- saved velocity and laws may influence generation;
- debt, counters, velocity, co-occurrence, and soma remain unchanged.

## CLI examples

Live field without persistence:

```bash
./bashformer.sh --weights weights/bashformer.bfw \
  --prompt 'The blood was ' --tokens 48 \
  --temperature 1.5 --seed 1 \
  --velocity run --method --trace
```

Seed a recovery experiment:

```bash
./bashformer.sh --weights weights/bashformer.bfw \
  --prompt 'The blood was ' --tokens 16 \
  --temperature 0.7 --velocity run --debt 6 --trace
```

Persistent field:

```bash
./bashformer.sh --weights weights/bashformer.bfw \
  --prompt 'Mina ' --tokens 32 --temperature 0.8 \
  --soma mina.soma
```
