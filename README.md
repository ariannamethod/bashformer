# bashformer

**v0.5.0 — sentences acquired resonance.**

> notorch asks whether you need PyTorch.  
> bashformer asks whether you need C at inference time.

**Bashformer** is a decoder-only Transformer trained by
[notorch](https://github.com/ariannamethod/notorch) and executed by GNU Bash.
The runtime performs embeddings, RMSNorm, grouped-query attention, RoPE, causal
softmax, SwiGLU, residual connections, a tied language-model head, sampling,
and a stateful Arianna Method field using Bash integer arithmetic and indexed
arrays. The field now carries sentence-level vector memory through Sentence
Phonon Attention (SPA), alongside token co-occurrence, prophecy debt, and laws.

No `awk`. No `bc`. No Python. No hidden C helper. No vendored framework.

## The split

| Stage | Engine |
|---|---|
| Training and export | installed upstream **notorch** |
| Weight format | BFW1, checked ASCII fixed-point tensors |
| Production inference | **GNU Bash builtins only** |
| Correctness oracle | independent integer C reference, tests only |

Notorch is installed under `${BASHFORMER_PREFIX:-$HOME/.local}` from the exact
commit pinned in `requirements.txt`. Its checkout is temporary: fetch, build,
install, delete. The repository contains no frozen `notorch.c`.

Backend selection follows the upstream acceleration ladder:

```text
macOS                         -> Accelerate
OpenBLAS available            -> OpenBLAS
x86_64 with AVX2 + FMA        -> Notorch in-house SIMD
otherwise                     -> scalar C
```

See [`docs/NOTORCH.md`](docs/NOTORCH.md).

## Architecture v0

The forge in `src/train.c` builds a deliberately tiny Llama-3-shaped character
model for Bram Stoker's *Dracula*:

```text
vocab       96 bytes (newline, space, printable ASCII)
context     64
d_model     32
layers      2
q_heads     4
kv_heads    1       # grouped-query attention
head_dim    8
ffn         64
rope theta  500,000
parameters  20,640
tied head   yes
```

Each block is:

```text
RMSNorm -> GQA(Q,K,V + RoPE) -> projection -> residual
RMSNorm -> SwiGLU -> projection -> residual
final RMSNorm -> tied WTE head
```

The Bash body uses signed Q12 fixed point. The exporter writes `exp`, sigmoid,
and RoPE lookup tables; RMSNorm uses integer Newton iteration. Production
inference therefore asks the shell to calculate no floating-point `exp`, `sin`,
`cos`, or `sqrt`.

## Prove it before trusting it

```bash
make test
```

The suite generates deterministic fixtures, runs the same model through the C
integer oracle and `bashformer.sh`, and compares exact stage checksums after:

- Q and K rotation;
- causal GQA output;
- each residual block;
- final logits;
- every sampled token.

It also verifies the Method field independently:

- DESTINY × PROPHECY, PAIN, FOCUS/SPREAD;
- entropy-floor and resonance-ceiling laws;
- five velocity modes and deterministic xorshift32 sampling;
- prophecy-debt accumulation and live `RUN -> NOMOVE` recovery;
- Hebbian H-term learning and debt-gated neuromodulated plasticity;
- Sentence Phonon Attention, sentence-history persistence, and connectedness modulation;
- BFSOMA1/BFSOMA2 migration and byte-identical BFSOMA3 persistence;
- `--learn off` and `--field off` non-mutation;
- malformed/injectable state rejection;
- inference with an intentionally empty `PATH`;
- dependency lifecycle, backend selection, doctor ABI, checkpoint, and BFW1
  export.

GCC and Clang are both accepted. The C oracle and Notorch doctor are also run
under AddressSanitizer and UndefinedBehaviorSanitizer during release validation.

## Install the real forge

```bash
make bootstrap
make doctor
```

`make doctor` links against the installed `libnotorch.a` and executes the exact
forge-facing contract: RMSNorm, GQA, explicit RoPE theta 500000, SwiGLU,
sequence cross-entropy, backward, gradient clipping, and Chuck.

Force a backend when needed:

```bash
NOTORCH_BACKEND=openblas make bootstrap
NOTORCH_BACKEND=simd     make bootstrap
NOTORCH_BACKEND=scalar   make bootstrap
```

Use an already cloned, pinned checkout without vendoring it:

```bash
NOTORCH_SOURCE=/path/to/notorch make bootstrap
```

Then run the short installed-upstream acceptance path:

```bash
make upstream-smoke
```

It trains the real Bashformer graph briefly, exports BFW1, and immediately runs
those weights in pure Bash.

## Train on Dracula

```bash
make corpus
make train
```

The corpus URL is pinned to an immutable `nanoGPT-notorch` commit. Control the
forge directly:

```bash
./build/bashformer-train \
  --corpus data/dracula.txt \
  --steps 20000 \
  --lr 3e-4 \
  --out weights/bashformer
```

Resume from `weights/bashformer.bin`:

```bash
./build/bashformer-train --resume \
  --corpus data/dracula.txt \
  --out weights/bashformer
```

Probe the float Notorch checkpoint:

```bash
./build/bashformer-train \
  --probe weights/bashformer.bin 'The blood was '
```

## Speak through Bash

Greedy generation:

```bash
./bashformer.sh \
  --weights weights/bashformer.bfw \
  --prompt 'The blood was ' \
  --tokens 32
```

Reproducible Q12 sampling:

```bash
./bashformer.sh \
  --weights weights/bashformer.bfw \
  --prompt 'CHAPTER I' \
  --tokens 32 \
  --temperature 0.8 \
  --seed 42
```

Inspect one prediction or the integer graph:

```bash
./bashformer.sh --prompt 'Mina ' --next-id
./bashformer.sh --prompt 'Mina ' --next-hex
./bashformer.sh --prompt 'Mina ' --next-id --trace
```

## Method field v0.5

The Method layer is implemented directly in Q12 Bash. Production inference does
**not** call `aml`, link `libaml.a`, or start another process.

```bash
./bashformer.sh \
  --weights weights/bashformer.bfw \
  --prompt 'The blood was ' \
  --tokens 48 \
  --temperature 1.5 \
  --seed 1 \
  --velocity run \
  --prophecy 12 \
  --destiny 0.35 \
  --pain 0.20 \
  --focus 0.80 \
  --spread 0.10 \
  --entropy-floor 0.10 \
  --resonance-ceiling 0.95
```

The per-token loop is now closed:

```text
Transformer logits
  -> Hebbian H (optional, stateful)
  -> SPA sentence connectedness (optional, stateful)
  -> DESTINY × PROPHECY
  -> PAIN
  -> ATTEND_FOCUS / ATTEND_SPREAD
  -> ENTROPY_FLOOR / RESONANCE_CEILING
  -> temperature × current VELOCITY
  -> seeded sampling
  -> chosen-token prophecy debt
  -> debt decay
  -> recovery: debt > 5 forces VELOCITY NOMOVE
  -> debt-gated Hebbian plasticity (optional, 1x..2x)
  -> sentence-boundary embedding commit (optional)
  -> next token observes the new state
```

A non-maximum choice incurs debt according to its distance from the peak:

```text
delta = max(logits) - logits[chosen]
debt_add = delta / (delta + 1)
debt = min(100, debt + debt_add)
debt = debt * debt_decay
```

Recovery is not metadata. If debt crosses 5 while the field is moving, velocity
changes to `nomove`; the following token is sampled at half the base temperature.
`--trace` prints the chosen token, added debt, total debt, recovery event,
velocity, effective temperature, field step, and recovery count.

The laws are also active transformations, not labels. They constrain the top-two
logit gap before sampling. Full equations and ordering are in
[`docs/METHOD_PHASE.md`](docs/METHOD_PHASE.md).

`--field off` restores the exact vanilla Transformer and prevents all state
mutation. `--learn off` may read and apply a saved field but freezes co-occurrence,
debt, velocity, counters, and the state file.


### Sentence Phonon Attention

```bash
./bashformer.sh \
  --weights weights/bashformer.bfw \
  --prompt 'The blood was warm. The night was ' \
  --tokens 32 --temperature 0.9 --seed 42 \
  --spa --spa-alpha 0.85 --spa-strength 0.30
```

At `.`, `!`, `?`, or newline, Bash condenses the completed sentence into a
Q12 exponentially weighted mean of token embeddings. Up to eight sentence
vectors are retained. The unfinished sentence attends over that history; the
maximum softmax attention probability is its connectedness. Connectedness drops
an internal logit temperature, sharpening sampling without touching BFW1
weights. The entire vector history and unfinished sentence survive through
BFSOMA3.

Debt may also modulate how strongly the H-term learns:

```bash
--hebbian --plasticity debt
```

The gain is `1 + debt / (debt + 5)`, bounded below 2x. `flat` preserves the
v0.3/v0.4 rule exactly.

## Persistent soma

```bash
./bashformer.sh \
  --weights weights/bashformer.bfw \
  --prompt 'Mina ' \
  --tokens 32 \
  --temperature 0.8 \
  --soma mina.soma
```

`BFSOMA3` persists:

- sparse Hebbian co-occurrence edges and the eight-token ring;
- prophecy debt and decay;
- entropy floor and resonance ceiling;
- current velocity;
- total field steps and recovery count;
- plasticity mode, gain telemetry, and update count;
- SPA enablement, coefficients, connectedness/temperature telemetry, sentence
  embeddings, and unfinished sentence.

The parser also reads `BFSOMA1` and `BFSOMA2` and migrates either on the next writable session.
The format is strict data, never sourced or evaluated. See
[`docs/SOMA.md`](docs/SOMA.md).

## Why `bashformer`, not `bashformer-notorch`

Notorch is the forge. Bashformer is the organism. The trained model remains a
real Transformer after the forge is removed from the room.

## Files

```text
bashformer.sh             Bash-only inference runtime
src/train.c               installed-Notorch training and BFW1 exporter
tools/reference.c         independent fixed-point C oracle
tools/notorch_doctor.c    installed-Notorch forge ABI probe
tools/mkfixture*.c        deterministic fixture generators
bootstrap.sh              ephemeral fetch -> make install -> source deletion
requirements.txt          pinned Notorch dependency
tests/parity.sh           vanilla stage parity
tests/method.sh           stateless field, velocity, RNG, and ablation parity
tests/stateful.sh         H-term and soma persistence/safety parity
tests/dynamics.sh         laws, debt, recovery, migration, and state gates
tests/spa.sh              SPA, plasticity, BFSOMA3, persistence, and safety
docs/BFW1.md              checked text weight format
docs/ARCHITECTURE.md      graph and exact integer contract
docs/NOTORCH.md           install, acceleration, doctor, air-gapped workflow
docs/METHOD_PHASE.md      field equations and per-token dynamics
docs/SOMA.md              BFSOMA3 persistent state format
docs/MOMENT_V05.md        learned-body SPA/plasticity acceptance run
```

## License

GPL-3.0-or-later.
