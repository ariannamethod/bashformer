# Changelog

## v0.5.0 — 2026-08-18 — Sentences acquired resonance

- Added pure-Bash Sentence Phonon Attention: exponentially weighted sentence
  embeddings, history attention, connectedness sharpening, and boundary commits.
- Added debt-gated Hebbian plasticity: `gain = 1 + debt/(debt+5)`, with `flat`
  mode preserving the prior learning law exactly.
- Upgraded persistent state to `BFSOMA3`, carrying sentence vectors, unfinished
  sentence tokens, SPA configuration/telemetry, and plasticity state.
- Added strict BFSOMA1/BFSOMA2 migration, v3 structural validation, injection
  rejection, read-only/field-off invariants, and exact Bash/C serialization parity.
- Added `tests/spa.sh`; connectedness, sampled trajectories, multi-process
  persistence, neuromodulation, bounds, and parser safety are pinned.
- Preserved the installed-upstream Notorch bootstrap/doctor/smoke gate. This
  sandbox still cannot fetch the pinned checkout, so no synthetic backend is
  relabeled as an upstream acceptance run.

## v0.4.0 — 2026-08-18 — Choices acquired consequences

- Added Q12 `ENTROPY_FLOOR` and `RESONANCE_CEILING` laws after the existing
  Hebbian/DESTINY/PAIN/FOCUS field pipeline.
- Added chosen-token prophecy debt from post-field logits:
  `delta / (delta + 1)`, capped at 100 and decayed once per emitted token.
- Closed the feedback loop: debt above 5 forces `VELOCITY NOMOVE`, and the next
  token observes the resulting half-temperature.
- Added `--debt`, `--debt-decay`, `--entropy-floor`, and
  `--resonance-ceiling`, with strict public bounds.
- Upgraded persistent state to `BFSOMA2`: Hebbian memory, debt, decay, laws,
  velocity, field-step count, and recovery count survive process boundaries.
- Added strict `BFSOMA1 -> BFSOMA2` migration; fixed a C-oracle header-index bug
  caught by the migration test.
- Added exact C/Bash law, sampled-debt, recovery, migration, persistence,
  read-only, empty-PATH, malformed-state, and bounds tests.
- Validated the release with GCC, Clang, ASan, and UBSan.

## v0.3.0 — 2026-08-16 — The shell learned to remember

- Notorch bootstrap now selects Accelerate, OpenBLAS, in-house AVX2+FMA SIMD,
  or scalar.
- Added pinned local `NOTORCH_SOURCE` installs for air-gapped or reused upstream
  checkouts.
- Added `make doctor`, a forge-facing GQA/RoPE/SwiGLU/backward/Chuck ABI probe.
- Added `make upstream-smoke` for installed-upstream train -> export -> pure-Bash
  wiring.
- Added live Method Hebbian co-occurrence H-term in Q12 Bash.
- Added strict persistent `BFSOMA1` state with parser safety, read-only mode,
  field-gated mutation, and exact C/Bash serialization parity.

## v0.2.0 — 2026-08-14 — The shell acquired a field

- Added an opt-in Q12 port of stateless Arianna Method inference physics:
  prophecy-scaled DESTINY, PAIN compression, and ATTEND_FOCUS/SPREAD.
- Added VELOCITY modes `raw`, `nomove`, `walk`, `run`, and `breathe` as explicit
  sampling-temperature multipliers.
- Replaced implicit `$RANDOM` sampling with a built-in, seedable xorshift32 path
  mirrored by the C oracle.
- Added `--field off` as a strict ablation gate; vanilla traces remain
  bit-identical.
- Added exact C/Bash Method traces, combined-pipeline tests, five velocity tests,
  seeded generation parity, empty-PATH proof, and public-bound rejection.

## v0.1.0 — 2026-08-14 — Bash learned to attend

- Added a two-layer, 20,640-parameter Llama-3-shaped Dracula forge backed by an
  installed, pinned upstream Notorch.
- Added BFW1, a non-executable checked ASCII Q12 weight format.
- Added GNU Bash builtins-only incremental inference: embeddings, RMSNorm,
  adjacent-pair RoPE, GQA with KV cache, causal LUT softmax, SwiGLU, residuals,
  tied head, greedy decoding, and fixed-point sampling.
- Added an independent C integer oracle with stage checksums.
- Added exact C/Bash parity at small and production geometry, malformed-model
  rejection, an empty-PATH runtime proof, dependency lifecycle tests, sanitizer
  checks during development, and an end-to-end forge/export contract smoke.
