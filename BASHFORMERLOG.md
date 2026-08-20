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

## 2026-08-20 — пин двинут на notorch main, стамп перестал врать

### Решение

Из трёх вариантов (оставить старый пин / двинуть пин / принимать системную
сборку) взят второй. Третий отклонён: у бинаря в `/opt/homebrew/lib` нет
ref-стампа, доказать происхождение нечем, а `bootstrap.sh` именно этим и
занят. Первый консервировал bashformer на коде, отставшем от notorch на 7
коммитов (`git rev-list --count a30329b8..HEAD`) — витрина, не ловящая
ABI-дрейф, витриной быть перестаёт.

Гейт перед сменой пина: собрать и проверить, потом двигать.
`~/arianna/notorch` стоит на `f0e6fc4`, но этот коммит живёт только в
`origin/claude/js-packed-matvec`; в main его нет. Пин на коммит из ветки
сломал бы `bootstrap.sh` любому клонирующему. Поэтому цель — `f0e4f73`,
голова `origin/main` notorch.

```
notorch-doctor: OK loss=3.420045 gqa=2:1 rope=500000 chuck=OK
build/bashformer-train  193552 B
```

Сетевой `./bootstrap.sh` по новому пину тоже проверен: `f0e4f73`
вычитывается с GitHub, стамп совпал.

### Найденный по дороге баг честности

`bootstrap.sh` собирал notorch из `f0e4f73`, а в `notorch.commit` и
`notorch.env` записывал **заявленный** пин `a30329b8`. `make doctor`
печатал этот ref как происхождение сборки — и врал. Причина:
`install_notorch` писал `$ref` из `requirements.txt`, а не фактический
HEAD источника. С `NOTORCH_ALLOW_UNPINNED=1` пин превращался в украшение.

Исправлено: `bf_checkout_source` вычисляет `BF_SOURCE_REF` через
`git rev-parse HEAD` источника (для обеих веток — и локальной, и сетевой);
стамп и `NOTORCH_REF` несут фактический коммит, рядом появился
`NOTORCH_PINNED_REF`, строка установки при расхождении говорит `UNPINNED`
вслух, а `tools/notorch_flags.sh ref` дописывает
`(unpinned; requirements pin …)`.

Красная проба до фикса:

```
NOTORCH_REF=a30329b8111e69926ea41e5ec4987ae7f0c18afc   ← записано
f0e4f73be9d58a7dac536655905c791fe015177a                ← собрано
```

После фикса:

```
installed notorch f0e4f73… UNPINNED (requirements pin a30329b8…) (accelerate) -> ~/.local
tools/notorch_flags.sh ref → f0e4f73… (unpinned; requirements pin a30329b8…)
```

### Проба на инвариант

В `tests/bootstrap.sh` добавлен блок: сборка с `NOTORCH_ALLOW_UNPINNED=1`
из источника, чей HEAD не совпадает с пином, обязана записать в стамп
фактический коммит, сказать `UNPINNED` в логе и показать пометку в
`notorch_flags.sh ref`. Тест фальсифицирован: с возвращённым
`local built=$ref` он краснеет, с фиксом — зелёный.

`make test` после всех правок: 8/8, 20.8 s.

### Открыто

- Обученного тела по-прежнему нет; `make train` не запускался.
- `~/arianna/notorch` остался на `f0e6fc4` — не трогался, сборка шла из
  отдельного клона в scratchpad на `f0e4f73`.
