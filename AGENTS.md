# AGENTS.md

Academic C++20 project (TFG): search algorithms for the Referee Assignment Problem (RAP).
Goal: maximize retribution per hour worked by reducing travel and idle ("tiempos muertos") time.
The domain model, `src/main.cpp`, the packed search state (`algorithms/estado.hpp`), the state arena (`algorithms/arena.hpp`), the objective function (`algorithms/objetivo.hpp`), the static-data construction (`algorithms/tiempo.hpp`, `algorithms/instancia_dia.hpp`, `algorithms/construir_instancia.hpp` and their `.cpp`) and their tests are the real code so far.

## Build & verify

- Intended build (README): `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- `cmake` is NOT installed in this environment. Verify manually instead:
  - Main: `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude src/main.cpp -o /tmp/opencode/referee_scheduler`
  - Tests (four executables, all must pass). Any test that touches the static-data code must compile the library sources too (`src/algorithms/*.cpp`):
    - `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude tests/test_smoke.cpp -o /tmp/opencode/t_smoke && /tmp/opencode/t_smoke`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_arena.cpp -o /tmp/opencode/t_arena && /tmp/opencode/t_arena`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_objetivo.cpp -o /tmp/opencode/t_obj && /tmp/opencode/t_obj`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_instancia.cpp -o /tmp/opencode/t_inst && /tmp/opencode/t_inst`
  - Header-only checks: compile a temp `.cpp` that includes the headers.
- `tests/CMakeLists.txt` registers `test_smoke.cpp` (`referee_scheduler_smoke`), `test_arena.cpp` (`referee_scheduler_arena`), `test_objetivo.cpp` (`referee_scheduler_objetivo`) and `test_instancia.cpp` (`referee_scheduler_instancia`). Keep them in sync.
- The OSRM client (phase 3) needs `libcurl4-openssl-dev`, **not installed yet** (`sudo apt install libcurl4-openssl-dev`). Until then, only the in-memory construction and a fake distance provider can be built; CMake must treat the OSRM target as optional (`find_package(CURL)`).

## Architecture

- Public headers in `include/referee_scheduler/{domain,algorithms,io}/`; implementations in matching `src/`; entrypoint `src/main.cpp`.
- `referee_scheduler_domain` is an `INTERFACE` (header-only) CMake target. Domain entities have no `.cpp`; do not add one.
- `referee_scheduler_algorithms` is a **`STATIC` target** (it stopped being `INTERFACE` when the static-data construction landed). It compiles `src/algorithms/*.cpp` and links `referee_scheduler_domain` PUBLIC. Header declarations stay in `include/`; logic goes in `src/`. Link it from anything that includes `algorithms/`.
- `referee_scheduler_io` (to be created in phase 3) will hold the OSRM client and link `CURL` when available. `algorithms/` must never depend on `io/`: the dependency is inverted through the `ProveedorDistancias` interface, which lives in `algorithms/`.
- All domain types are in `namespace referee_scheduler::domain`. Includes use the `referee_scheduler/domain/x.hpp` prefix (`-Iinclude` adds the root).
- Search/algorithm types live in `namespace referee_scheduler::algorithms` (`include/referee_scheduler/algorithms/`). They must not depend on `domain::Estado`.

## Conventions (differ from C++ defaults)

- Domain entities are `class` with **public fields**, no getters/setters, and default member initializers (`int id = 0;`). Data will be assigned by the future IO loader; do not reintroduce encapsulation/accessors unless asked.
- Spanish naming throughout: types `Equipo, Sede, Arbitro, Partido, Plaza, Asignacion, Estado`; fields like `sede_id`, `plaza_id`, `equipo_local_id`.
- Cross-entity references use `int` ids, not nested objects or pointers.
- A `Partido` requires one or more `Plaza` (one per role in `enum class Rol`). `nivel_requerido` and `retribucion` live on `Plaza`, not `Partido`. `Asignacion` is the pair (`plaza_id`, `arbitro_id`).
- `Arbitro` declares `disponibilidad_inicio`/`disponibilidad_fin` (no separate max-hours cap). Referee location/time is derived from the last assigned match's `sede_id`/`final`.

## Search state (decided)

- The search state is `algorithms::Estado` in `include/referee_scheduler/algorithms/estado.hpp`: `std::array<EstadoArbitro, kMaxArbitros>`, 2400 B, trivially copyable, no allocations. It is 4 B per referee, so 16 referees fit in a 64 B cache line and the full state spans 38 lines. `Estado` is the **maximum-capacity view** (initial state, debug, tests); the search nodes are stored by `algorithms::Arena`.
- `EstadoArbitro` is exactly 4 B: `sede_id : 10`, `minuto_liberacion : 11`, `reservado : 11` (10 + 11 + 11 = 32). `static_assert` enforces `sizeof(EstadoArbitro) == 4`, `sizeof(Estado) == 2400` and trivial copyability. The bitfield layout is implementation-defined; the size assertion is the guarantee. Do not add fields without redoing the bit budget.
- The state is the **minimal sufficient statistic** of the past: where each referee is, when he becomes free, plus 11 reserved bits. It stores no history (teams already refereed, match counts, etc.). Any per-referee rule needing history does not fit in 32 bits.
- `minuto_liberacion` is an absolute minute from the day's `t0`. A day is 1440 min, so 11 bits (2047) suffice with margin.
- `sede_id` has no sentinel: the initial state sets each referee at his domicile (`Arbitro::sede_id`) and free from `disponibilidad_inicio`.
- `reservado` is unused by design. There is **no per-referee retribution cap**; the field is a bit budget for a future constraint and can be subdivided without changing `sizeof`.
- `kMaxArbitros = 600` is a compile-time **capacity**, not a count. The real count `n_arbitros` is runtime data (in the instance). Only `[0, n_arbitros)` is meaningful. Never loop to `kMaxArbitros`. Assert `n_arbitros <= kMaxArbitros` on load: unused slots are zero, and a zero slot looks like a valid referee (sede 0, minute 0).
- Execution is **per day**: Friday, Saturday and Sunday are independent and run in parallel. Each day builds its own instance and state. Per-day **referee compaction** is the agreed direction: `Estado` indexes the day's available referees `0..N_dia-1`, not global ids. Optional per-day **sede compaction** only frees bits (10 -> 6/7), not bytes.
- `domain::Estado` (`vector<Asignacion>` + `ultima_sede_arbitro` + `franja_ocupada_arbitro`) is **legacy and superseded** by `algorithms::Estado`. Do not extend it. Its fate (delete, or keep as the solution type) is undecided; ask before changing it. The `franja` concept is superseded by continuous minutes.

## Arena (decided, v1 implemented)

- `algorithms::Arena` (`include/referee_scheduler/algorithms/arena.hpp`) stores search nodes with **stable addresses**. It receives `n_arbitros` at construction; it does not depend on the loader or the future `Instancia`.
- Each node is a contiguous block of `24 + 4·n_arbitros` bytes: a 24 B `CabeceraNodo` followed by `n_arbitros` `EstadoArbitro`. The stride is `calcular_tam_bloque(n)`, rounded up to 8 B. So a node costs 504 B at n=120 and 2424 B at n=600, not the 2400+24 of the worst case.
- `CabeceraNodo` is exactly 24 B and cannot change size without changing the address formula: `coste_g` (int64, objective step 2), `padre`, `nivel`, `flags`, `reservado`. `kSinNodo` marks the root's parent.
- `estado(nodo)` returns `std::span` of length `n_arbitros()`: it is structurally impossible to iterate the padding slots. `crear(Estado, ...)` copies only `[0, n_arbitros)` and ignores the tail.
- Nodes live in heap blocks of 64 KiB, held by `unique_ptr`; blocks never move, the block pointer vector may. Growth doubles the block count (amortized O(1)); moving the `Arena` keeps node pointers valid. Not copyable.
- Node indices are `std::uint32_t` and are permanent. `crear` asserts `n_arbitros <= kMaxArbitros`; address is `nodo / nodos_por_bloque` (current v1).
- Tests: `tests/test_arena.cpp` covers stride formula, n ∈ {1, 5, 300, 600}, copying only the prefix, span length, pointer stability across ~20 blocks and memory counters.
- Studied micro-optimizations (zeroing, division, hugepages/TLB) are **not implemented**: see `docs/optimizaciones.md`. They will be explored once Dijkstra works and can be measured.

## Objective function (decided, implemented)

- `coste_asignar` and `coste_dejar_vacia` live in `include/referee_scheduler/algorithms/objetivo.hpp`: header-only, no allocations. They depend on `EstadoArbitro` and `domain::Rol`, never on `domain::Estado`. Lower cost is better (minimization).
- Everything is counted in **integer cents** (`std::int64_t`); `a_centimos(double)` rounds euros to cents once. `kCentimosPorMinuto = 14` (8,4 €/h) and `kEpsilonCentimos = 1`; `ParametrosObjetivo` lets tests and future experiments override both.
- Assignment cost: `ε + (tiempo_viaje + espera_facturable) · k + (retorno_nuevo − retorno_anterior) · k`. Waiting is free while the referee is at his domicile; the return adjustment is prospective and telescopes, so a chain of assignments pays one real return trip at the end and every edge stays `>= ε` with a metric matrix (Dijkstra-safe).
- A late arrival (`llegada > minuto_inicio_plaza`) returns `nullopt`: the transition is infeasible and the generator must not create that child. Negative waits are never allowed to cheapen a route.
- Covered plazas do **not** add their retribution: the objective charges only time. Leaving a plaza empty with `Rol::Principal` is forbidden (`nullopt`); any secondary role costs its full retribution in cents (no epsilon).
- Future idea, **not implemented**: when the gap allows `ida + vuelta + 1 h en casa`, evaluate the same assignment "via home" (`sede = domicilio`, `liberacion += d(actual, domicilio)`) and keep the cheaper of the direct and via-home costs, so long gaps are charged as travel instead of dead time. Detailed in `docs/mejoras-practicas.md`; it needs the travel matrix and the transition generator.
- Tests: `tests/test_objetivo.cpp` covers cents conversion, first match, en-route wait, positive/negative return adjustment, late arrival, custom k/ε and empty plazas.

## Static data (decided; phases 1–2 implemented)

The design is five sequential phases that prepare the structures for the search. They are **in-memory only** for now; the file loader is step 5f.

- **Input aggregate**: `domain::Instancia` (`domain/instancia.hpp`) holds `equipos`, `sedes`, `arbitros`, `partidos`, `plazas`. Header-only, public fields, references by global `int` id.
- **`Sede` has `latitud`/`longitud`** (WGS84, degrees). Travel times come from the **OSRM** `/table` API, not from a matrix stored in the instance. Both coordinates 0 means "no real coordinates".
- **Time utilities** (`algorithms/tiempo.hpp` + `src/algorithms/tiempo.cpp`): `DiaCivil`, `dia_de(time_t)` (local time via `localtime_r`), `minuto_del_dia`, `indice_dia` (Howard Hinnant civil-days, no `time_t/86400`), `minuto_absoluto` (`indice_dia*1440 + minuto`) and `minuto_en_dia` (clamped to `[0, 1440]`). This is the only timezone-dependent code. `kMinutosPorDia = 1440`. **`t0` is 00:00 of the day**, so minutes are `hour*60 + minute` and a night match may exceed 1440 but stays below 2048.
- **Phase 1, partition by day** (`GrupoDia`, `agrupar_por_dia`): groups `partidos` by the **civil date of `inicio`** (never `final`), returns days sorted and, inside a day, positions in input order. A Friday 23:30 match ending Saturday 01:00 belongs to Friday. Indices are `std::size_t` positions in `Instancia::partidos`, no copies.
- **Phase 2, referee compaction** (`ArbitroDia`, `compactar_arbitros`): a referee belongs to the day if his availability window (in **absolute minutes**) contains a valid **start** minute of that day (`[dia*1440, dia*1440+1439]`). Comparing already-clamped minutes would wrongly include a referee available only the previous day. `primer_inicio`/`ultimo_inicio` are the window clamped to `[0, 1440]`; **1440 means no limit**. Result sorted by `id_global`, so the compacted id (vector index) is deterministic; the index is `0..N_dia-1`. `equipos_vetados` keeps only teams playing that day, sorted and deduplicated (binary search later).
- **Availability semantics (user-decided)**: the declared end is a bound on the **match start**, not on the match end. Available until 23:00 ⇒ a match starting at 23:00 is allowed even if it ends after midnight. The declared start means "available at his domicile from then", so the first trip is charged from `primer_inicio`.
- **Phase 3 (next, not implemented)**: `ProveedorDistancias` interface in `algorithms/` (`Coordenada`, `uint16_t` minutes, `kMinutosInalcanzable = 65534`); `OsrmDistancias` in `io/` (libcurl, batches of **100** coordinates, `sources`/`destinations` for larger sets, `lon,lat` order, `null` → unreachable, cache in `data/generated/`). Sedes are compacted per day to `0..M-1` (match sedes ∪ referee domiciles). The matrix is **full and asymmetric** (verified: OSRM gives different times per direction), never mirrored, diagonal forced to 0, `ceil(seconds/60)` (ceiling preserves the triangle inequality). Optional **Floyd–Warshall metric closure** (default on) because real road networks are not strictly metric and the objective needs `coste >= ε`.
- **Phase 4 (planned)**: expand partidos into `PlazaDia` sorted by `(minuto_inicio, sede, partido_id, plaza_id)`; index = tree level; `es_principal = (rol == Principal)`; carry `minuto_inicio`, `minuto_fin`, `nivel_requerido`, `retribucion_centimos` (`a_centimos` from `objetivo.hpp`) and both team ids.
- **Phase 5 (planned)**: per-plaza candidate lists in a flat CSR (`std::vector<uint32_t>` + offsets of size `n_plazas()+1`). Filters: `nivel >= nivel_requerido`, `primer_inicio <= minuto_inicio <= ultimo_inicio`, no veto on either team, and **reachability** `primer_inicio + d(domicilio, sede) <= minuto_inicio` (plus `d != kMinutosInalcanzable`).
- Tests: `tests/test_instancia.cpp` covers phases 1 and 2 (and the time utilities). It must grow one block per phase.

## Roadmap / scope

Agreed order:

1. **Arena** for state storage: stable block storage; block size `24 + 4·n_arbitros` computed at run time; the arena receives `n_arbitros`, it does not depend on the loader. **v1 implemented** (`algorithms/arena.hpp`); v2 micro-optimizations deferred to `docs/optimizaciones.md`.
2. **Objective function**: additive and positive, in integer cents; `ε + tiempo · k` with k = 14 c/min, return-trip adjustment, and empty secondary plazas charged their lost retribution. **Implemented** (`algorithms/objetivo.hpp`); the travel lookup is filled in step 5.
3. **State transitions and operations**: the 4-byte write per assignment; decision fields of the node.
4. **Search frontier / open list**.
5. **Static data**:
   a. In-memory instance (`n_arbitros`, `t0`, referees, matches, plazas). **Implemented**: `domain::Instancia`, `domain::Sede` coordinates.
   b. Per-day referee compaction (global id -> `0..N_dia-1`); optional sede remap. **Referee compaction implemented** (`compactar_arbitros`); **sede compaction pending** (phase 3).
   c. Travel-time matrix (or sede coordinates + formula). **Decided**: OSRM `/table`; pending implementation in phase 3.
   d. Minute offsets for match start/end. **Implemented**: `tiempo.hpp` with `t0 = 00:00`.
   e. Chronological ordering of plazas. **Pending** (phase 4).
   f. Per-plaza candidate lists (CSR + reachability). **Pending** (phase 5).
   g. Instance loader / file format (`data/instances/`). **Pending** (step 5f, after the phases).
6. **Assemble Dijkstra / A\***.
7. **Final validation**: recompute constraints and cost from the reconstructed assignments.

## Known gaps / pending decisions

- Phase 3 must decide the OSRM endpoint/perfil and whether the metric closure is on by default (agreed: `ceil` rounding, full asymmetric matrix, cache in `data/generated/`). `Sede` now has coordinates; a sede with both at 0 is a bug.
- `libcurl4-openssl-dev` is **not installed**; the user must run `sudo apt install libcurl4-openssl-dev` before the OSRM client can be built. Keep CMake working without it (optional target).
- `Arbitro::disponibilidad_*` are `time_t`; the conversion is done in `tiempo.hpp`. The declared start means "available at home from then" (not "can leave home earlier to arrive on time"): that would need a per-referee departure field that does not fit in `EstadoArbitro`.
- `kMaxArbitros = 600` is a hard cap; if the real instance exceeds it, that is a bug. The 10-bit `sede_id` requires `M_dia <= 1024`, guaranteed by per-day sede compaction.
- Node budget / memory limit, tie-breaking/determinism, and the parallel mechanism for the three days are undefined.
- Tests cover smoke, arena, objective and static-data phases 1–2; grow them as phases 3–5 land.
- No benchmark or baseline yet; `docs/optimizaciones.md` proposes a minimal arena benchmark to be added once the search runs.

## Rules

- Always add the necessary comments for the user to understand the newly created or modified code.
- Wait until the user has approved all the changes. Once approved, proactively ask if I should add exhaustive comments and documentation across all the modified code.
  -Only whenn I asked you for the commit message you will have to update the context
