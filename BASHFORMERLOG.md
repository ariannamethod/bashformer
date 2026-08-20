# BASHFORMERLOG

Рабочий лог bashformer. Каждая запись — что сделано, чем доказано, что осталось
открытым. Хронология сверху вниз.

## 2026-08-20 — приёмка на neo (Claude, Arianna Method)

### Что было

Публичный репозиторий `ariannamethod/bashformer` на `298080f` содержал 15
файлов и **не собирался**: `Makefile` ссылается на `tests/*.sh`, `tools/*.c`,
`data/fetch_dracula.sh`, которых в дереве нет. `README.md` объявлял v0.3.0 при
`BF_VERSION=0.5.0` в коде (`bashformer.sh:8`). `ARCHITECTURE.md` и `BFW1.md`
лежали в корне, хотя README ссылается на них как на `docs/`.

### Влито из v0.5.0

`tests/` (9 файлов), `tools/` (5), `data/` (2), `weights/.gitkeep`,
`.gitignore`, `docs/MOMENT_V05.md`, обновлённые `Makefile` (знает
`dynamics.sh`/`spa.sh`/`run_all.sh`) и `README.md` (v0.5.0, 352 строки).
`bashformer.sh`, `bootstrap.sh`, `src/train.c` совпали с репозиторием по md5 —
не трогались.

Структура по решению Олега: все `.md` кроме `README.md` и этого лога — в
`docs/`; `CHANGELOG.md` → `docs/CHANGELOG.md`. Корпус переехал
`dracula.txt` → `data/dracula.txt` (873579 B) и снят с гита: `.gitignore`
автора игнорирует `data/dracula.txt`, качается через `make corpus`.

### Три починки

1. **Exec-бит.** `bashformer.sh` и `bootstrap.sh` лежали в индексе как
   `100644`. Из свежего клона `./bashformer.sh` давал `Permission denied`,
   `tests/parity.sh` и `tests/bootstrap.sh` падали. Исправлено
   `git update-index --chmod=+x` → `100755`.

2. **Хардкод `/bin/bash`.** Четыре теста проверяют «только builtins» через
   `PATH=/definitely-empty /bin/bash`. На macOS `/bin/bash` — 3.2.57, где нет
   `declare -A` и `[[ -v ]]`; `parity`, `method`, `stateful`, `dynamics`
   падали с синтаксической ошибкой на `bashformer.sh:97`. Заменено на
   `"$BASH"` — текущий интерпретатор, пустой PATH сохранён, строгость теста
   не ослаблена.

3. **`SHELL := /bin/bash` в Makefile** → `SHELL := $(shell command -v bash)`.
   Проект требует bash ≥ 4.3 (`bashformer.sh:54-58`), а make брал системный
   3.2.

На neo поставлен GNU bash 5.3.15 (`brew install bash`) — до этого в системе
был только `/bin/bash` 3.2.57, то есть bashformer здесь не запускался вообще.

### Доказательства

`make test` — 8/8 зелёных, 30.9 s wall:

```
parity: exact stage checksums and greedy tokens agree (6 small prompts + production geometry); parser guards pass
method: destiny, pain, attention, field gate, and 5 velocity modes match C exactly; seeded sampling and bounds pass
bootstrap: notorch + optional Method install, stamps, reuse, and success/failure cleanup pass
forge: trainer control flow, checkpoint, BFW1 export, vanilla parity, and Method parity pass
stateful: H-term, live learning, BFSOMA3 persistence/read-only mode, field gate, parser safety, and C parity pass
dynamics: laws, sampled debt, live recovery, BFSOMA3 persistence/migration, state gates, parser safety, and exact C parity pass
spa: sentence embeddings, connectedness modulation, debt-gated plasticity, BFSOMA3 migration/persistence, parser safety, and exact C parity pass
doctor: forge-facing GQA/RoPE/SwiGLU/backward/Chuck ABI probe passes
```

Живой прогон на `weights/fixture-prod.bfw` (случайная фикстура, не обученное
тело), 24 байта, temp 0.7, seed 42, 1.41 s:

```
The blood was     5555555555555<@K6666
```

Мусор — ожидаемо: у фикстуры нет обучения. Проверялась работоспособность
петли, не качество.

Инференс без единого внешнего бинаря:

```
PATH=/definitely-empty bash ./bashformer.sh --weights weights/fixture-prod.bfw --prompt AB --next-id  → 25
./build/reference       --weights weights/fixture-prod.bfw --prompt AB --next-id                      → 25
```

### Открыто

- **Обученного тела нет.** `docs/MOMENT_V05.md` описывает 20 640-параметровое
  Q12 тело (loss 4.6406 → 0.4187), но весов в репозитории нет и быть не
  должно (`.gitignore`). Воспроизводить — через `make bootstrap && make train`.
- **notorch: пин против системы.** `requirements.txt` пинит
  `a30329b8111e69926ea41e5ec4987ae7f0c18afc`; локальный чекаут
  `~/arianna/notorch` на `f0e6fc4`, системная сборка стоит в
  `/opt/homebrew/lib/libnotorch.a`. `bootstrap.sh` ставит в `$HOME/.local` и
  пин проверяет. Решение по системной линковке — за Олегом.
- Ни `bootstrap`, ни `train` в этом ходе не запускались.
