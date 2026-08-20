# BASHFORMERLOG

Рабочий лог bashformer. Каждая запись — что сделано, чем доказано, что осталось
открытым. Хронология сверху вниз.


## История версий до приёмки на neo


### v0.5.0 — 2026-08-18 — Sentences acquired resonance

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

### v0.4.0 — 2026-08-18 — Choices acquired consequences

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

### v0.3.0 — 2026-08-16 — The shell learned to remember

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

### v0.2.0 — 2026-08-14 — The shell acquired a field

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

### v0.1.0 — 2026-08-14 — Bash learned to attend

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
`docs/`; содержимое `CHANGELOG.md` влито в этот лог. Корпус переехал
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

## 2026-08-20 — переполнение, которое не ловил ни один тест

### Находка

`bf_validate_metadata` проверяет каждый параметр по отдельности, но никогда —
их произведение. Загрузчик ограничивает **веса** значением
`BF_VALUE_LIMIT` = 2^26; активации не ограничены ничем, а именно они выходят из
`bf_matvec` и растут через residual.

Инструментированная копия (оригинал не трогался) на честной v0-геометрии:

```
max|q|=4148   max|raw|=13461790   max|scaled|=19274550992   int64max=9223372036854775807
```

Запас порядка 2^29. На модели, которую загрузчик **принимает** — все веса ровно
2^26:

```
max|q|=48619029790720   max|raw|=0
```

`raw` — сумма восьми произведений `q*k` одного знака, по модулю ~2^45. Нулём она
быть не может; она равна нулю ровно потому, что произведения кратны 2^64 и
wraparound съедает их целиком.

Оба движка выдали одинаковый `next-id 0`. `tools/reference.c` считает тем же
`int64_t` без защиты, так что parity сравнивал два одинаково сломанных пути:
в Bash — wraparound, в C — undefined behaviour. Первый же другой флаг
оптимизации развёл бы их, и «эталон» перестал бы быть эталоном.

### Что сделано

`bf_activation_limit` выводит из метаданных предел активации, при котором ни
одна стадия не выходит за signed 64-bit — минимум по пяти редукциям (matvec,
rmsnorm, скоры внимания, смешивание значений, rope/swiglu), потолок взят 2^62
ради бита запаса. Для v0 предел = 19952650 при наблюдаемом пике 4148.

`bf_check_act` проверяет выход каждой стадии в `bf_decode_token` — 13 точек:
эмбеддинг, обе rmsnorm, q/k/v, attention, оба residual, gate/up, swiglu,
финальная rmsnorm. Индукция замкнута: входы ограничены, значит внутренние
произведения не переполняются, а разрастание ловится на выходе.

C-оракул получил то же самое: `activation_limit`, `check_act`, плюс отсутствовавшая
у него проверка `VALUE_LIMIT` при загрузке. Оба движка теперь отказывают
одинаково и одним текстом:

```
bashformer: embedding: activation 67108864 exceeds the 64-bit fixed-point contract (limit 19952650)
reference:  embedding: activation 67108864 exceeds the 64-bit fixed-point contract (limit 19952650)
```

### Пробы

`tests/parity.sh` строит из production-фикстуры модель с насыщенным WTE и
требует, чтобы **оба** движка её отвергли, и чтобы текст отказа совпал после
снятия префикса имени. Фальсифицировано дважды: убран гвард в Bash — сьют
краснеет; убран гвард в C — сьют краснеет; с обоими — зелёный.

Цена гварда, 24 токена на v0-геометрии, три прогона каждого варианта:

```
guarded    3.07s  3.23s  3.28s
unguarded  3.22s  3.25s  3.26s
```

В пределах шума: проверка O(n) на фоне редукций O(n²).

`make test` — 8/8, 21.3 s. Честная фикстура даёт тот же ответ, что и до правки
(bash 25 = C 25), генерация не изменилась.

### Открыто

- Предел выводится из метаданных, но не проверяется на этапе экспорта весов:
  форж может выпустить BFW1, который загрузчик примет, а рантайм отвергнет на
  первом же токене. Логичное продолжение — та же проверка в `src/train.c` при
  экспорте.

## 2026-08-20 — контракт добрался до форжа

`quantize` (`src/train.c`) клампил результат к `INT_MAX` = 2^31, тогда как
загрузчик отвергает всё, что больше `BF_VALUE_LIMIT` = 2^26. То есть форж мог
отработать полный прогон и выдать чекпоинт, который его собственный рантайм не
читает. Отдельно: `WTE` — одновременно вес и первая активация (копируется прямо
в residual), поэтому для неё действует более тесная граница из
`bf_activation_limit`.

`emit_int_tensor` теперь проверяет каждое значение против нужной границы —
`BF_VALUE_LIMIT` для весов, `min(BF_VALUE_LIMIT, BF_ACT_LIMIT)` для `WTE` — и
падает с именем тензора и индексом вместо того, чтобы писать нечитаемый файл.
Формула предела продублирована из `bashformer.sh` (`bf_activation_limit`),
чтобы форж и рантайм отвечали одному контракту.

Красная проба: лимит временно урезан до 3, честные веса его нарушают —

```
bashformer-train: WTE[0] = 145 exceeds the runtime contract (limit 3); this checkpoint would not load
```

С восстановленным лимитом `tests/forge.sh` зелёный, `make test` 8/8.

### Побочный урок, не про bashformer

Красная проба запустила `./build/bashformer-train --steps 2` — реальный, пусть
и двухшаговый, прогон обучения, и мой собственный хук его пропустил: паттерн
ловил `train`, `train_*`, `./train`, а имя `bashformer-train` под них не
подходило. Сузив детектор утром, я проверил ложные срабатывания и не проверил
пропуски. Закрыто отдельным коммитом в `~/.claude`.

## 2026-08-20 — три файла ушли, остальные остались осознанно

37 → 34 файла, и все три удаления убрали дублирование, а не содержание.

- `tools/mkfixture_prod.c` был двухстрочной обёрткой: `#define
  BF_PRODUCTION_GEOMETRY 1` плюс `#include "mkfixture.c"`. Это обход сборки
  файлом; теперь та же цель собирается из `tools/mkfixture.c` с
  `-DBF_PRODUCTION_GEOMETRY=1` прямо в `Makefile`.
- `weights/.gitkeep` держал каталог, который `Makefile` и так создаёт
  (`mkdir -p $(WEIGHTS)` в обоих рецептах фикстур). Проверено сборкой из
  чистого дерева после `make clean`.
- `docs/CHANGELOG.md` влит в этот лог разделом «История версий до приёмки на
  neo». Два журнала на репозиторий — это два места, где расходится правда.

`make test` 8/8 после каждого шага, `README.md` синхронизирован.

### Что резать не стал и почему

- `docs/MOMENT_V04.md` перекрыт пятой версией, но описывает другой прогон
  приёмки. Удаление истории — не минимизация, а потеря.
- Восемь тестовых сьют можно схлопнуть в один файл и выиграть семь строк в
  дереве. Цена — `tests/run_all.sh` запускает их восемью процессами
  параллельно; слитый файл превратит 21 s в сумму. Плохая сделка.
- `requirements.method.txt` отделён от `requirements.txt` потому, что
  ariannamethod.ai — опциональная зависимость: «Vanilla Bashformer does not
  need this». Слияние сделало бы её обязательной.
- `tests/doctor.sh` — двенадцать строк, но проверяет установленный notorch,
  а не наш код. Разные субъекты, разные причины падения.

## 2026-08-20 — веса стали выражением, генерация ускорилась в 2.5 раза

### Где сидело время

`bf_matvec` делал хэш-лукап на каждый вес: `w[base+c]` внутри двойного цикла.
Микробенчи на 512K умножений:

```
цикл с лукапами массива          0.897 s   1.75 мкс/умножение
одно арифметическое расширение   0.171 s   0.33 мкс/умножение
```

Разница не в арифметике — она одинаковая. Разница в том, сколько раз
интерпретатор ходит в хэш-таблицу.

### Приём

Bash в арифметическом контексте раскрывает **имя переменной как выражение**.
Значит строку матрицы можно один раз собрать в текст
`37*BF_IN[0]+74*BF_IN[1]+…` и потом вычислять как `(( t = e ))`. Никакого
`eval`, никаких внешних процессов — контракт «только builtins» цел.
`BF_IN` — nameref, который `bf_matvec` привязывает к своему входному вектору;
проверено, что раскрытие внутри выражения через nameref работает.

Нулевые веса выбрасываются при компиляции: они ничего не добавляют, но стоят
умножения на каждом токене. В production-фикстуре их 0.3% (67 из 20480), на
обученном теле может быть больше.

После компиляции сырые массивы весов освобождаются — держать обе формы значит
удвоить память впустую. `WTE` остаётся массивом, потому что это ещё и таблица
эмбеддингов, читаемая построчно.

Сдвиг с округлением вписан прямо в выражение тернарником вместо вызова
`bf_qshift` — вызов функции в bash стоит ~2.2 мкс, сравнимо с умножением.

### Числа

```
                 load+1 токен   per-token
до                 0.523 s        65 ms
после              0.581 s        26 ms      -> генерация 2.5x
```

Компиляция стоит +58 ms на 20 640 весов при загрузке; на 9M это порядка 25 s
разово. Пик RSS 9664 -> 10192 KB, то есть +5.5%.

Выигрыш на v0 меньше пятикратного, потому что при DIM 32 / FFN 64 matvec — не
вся стоимость токена. На геометрии 9M его доля порядка 99%, там ускорение
будет ближе к замеренному пределу.

### Пробы

`make test` 8/8. Выход бит-в-бит совпадает с прежней реализацией на том же
seed, и C-оракул отвечает тем же токеном (25 = 25) — арифметика не изменилась,
изменилось только то, как интерпретатор до неё добирается.

### Что отброшено

- Считать всю матрицу одним расширением через оператор-запятую: 0.551 -> 0.538,
  два процента. Вход в арифметический парсер и так дёшев.
- Параллельность процессами: 4 фоновых подоболочки дали 1.9x на шести ядрах,
  но результаты пришлось бы гонять через файлы. Отложено, не отвергнуто.

## 2026-08-20 — геометрия Дубровского, и корпус, который считался потерянным

### Корпус нашёлся в истории

`TRAINING_LOG.md:11` в `~/arianna-datasets/dubrovsky/` утверждает: оригинальный
`dubrovsky.txt`, 1.17 MB / 3231 пара — «confirmed lost — not on disk anywhere
searched». Он не потерян. Он удалён коммитом `3c665fc` «Remove dataset and
training scripts from repo» в `~/arianna/dubrovsky`, и git его помнит:

```
git show 3c665fc^:dubrovsky.txt   ->  1172171 B, 3237 строк
```

Искали по файловой системе, а не по истории. Восстановлен в
`~/arianna-datasets/dubrovsky/dubrovsky.txt`, md5
`7792a435f65ef0f76542706362390b05`.

Итоговый корпус для char-level: оригинал плюс 200 новых пар из
`dubrovsky_contemporary.jsonl` (проверено: ни один их вопрос не встречается в
оригинале, тогда как `plain` вложен в него — 480 из 481). Длинные тире и
диакритика транслитерированы, байтов вне vocab 96 — ноль.
`dubrovsky_bashformer_charlevel.txt`, 1 284 274 B.

### Геометрия

1.28 MB по правилу Карпати (~1.1 MB на ~10M параметров) даёт около 11.7M.
Взято: DIM 256, 12 слоёв, 8Q/2KV (HEAD_DIM 32), FFN 768, CTX 256, vocab 96,
Q12 — **9 074 944 параметра**.

CTX поднят с 64 до 256 против первоначального плана: в корпусе ответы длиной в
сотни символов, и при окне 64 модель никогда не увидит вопрос вместе с ответом.
Цена мала — внимание даёт 8x256x32x2 = 131072 умножения против 9.04M в matvec,
то есть около 1.4% стоимости токена.

### Константы форжа стали переопределяемыми

`tests/forge.sh` держал литерал `M LAYERS 2`, то есть ломался от любой смены
геометрии вместо того, чтобы её проверять. Теперь константы `src/train.c`
обёрнуты в `#ifndef`, тест собирает форж на маленькой геометрии через `-D` и
сверяет экспорт **с той геометрией, которую собрал** — по семи ключам.
Иначе сьюта тратила бы минуты на загрузку 9M весов в Bash.

Фальсифицировано честно: первая попытка была негодной — я поменял и сборку, и
ожидание разом, поэтому они совпали. Настоящая проба — заставить экспорт
писать `BF_LAYERS + 1`: тест краснеет, с исправленным экспортом зелёный.

`make test` 8/8 на новой геометрии, форж собран (193696 B).
