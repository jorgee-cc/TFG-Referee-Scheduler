# AGENTS.md

Academic C++20 project (TFG): search algorithms for the Referee Assignment Problem (RAP).
Goal: maximize retribution per hour worked by reducing travel and idle ("tiempos muertos") time.
The domain model, `src/main.cpp`, the packed search state (`algorithms/estado.hpp`), the state arena (`algorithms/arena.hpp`), the objective function (`algorithms/objetivo.hpp`), the static-data construction (`algorithms/tiempo.hpp`, `algorithms/instancia_dia.hpp`, `algorithms/construir_instancia.hpp`, `algorithms/proveedor_distancias.hpp`), the OSRM client (`io/osrm.hpp`) and their tests are the real code so far.

## Build & verify

- Intended build (README): `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- `cmake` is NOT installed in this environment. Verify manually instead:
  - Main: `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude src/main.cpp -o /tmp/opencode/referee_scheduler`
  - Tests (five executables, all must pass). Any test that touches the static-data code must compile the library sources too (`src/algorithms/*.cpp`):
    - `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude tests/test_smoke.cpp -o /tmp/opencode/t_smoke && /tmp/opencode/t_smoke`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_arena.cpp -o /tmp/opencode/t_arena && /tmp/opencode/t_arena`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_objetivo.cpp -o /tmp/opencode/t_obj && /tmp/opencode/t_obj`
    - `g++ ... -Iinclude src/algorithms/*.cpp tests/test_instancia.cpp -o /tmp/opencode/t_inst && /tmp/opencode/t_inst`
    - OSRM (needs libcurl): `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude $(pkg-config --cflags libcurl) src/algorithms/*.cpp src/io/osrm.cpp tests/test_osrm.cpp -o /tmp/opencode/t_osrm $(pkg-config --libs libcurl) && /tmp/opencode/t_osrm`
  - Header-only checks: compile a temp `.cpp` that includes the headers.
- `tests/CMakeLists.txt` registers `test_smoke.cpp` (`referee_scheduler_smoke`), `test_arena.cpp` (`referee_scheduler_arena`), `test_objetivo.cpp` (`referee_scheduler_objetivo`), `test_instancia.cpp` (`referee_scheduler_instancia`) and, only if `referee_scheduler_io` exists, `test_osrm.cpp` (`referee_scheduler_osrm`). Keep them in sync.
- `libcurl4-openssl-dev` **is installed** (headers in `/usr/include/x86_64-linux-gnu/curl/`). CMake finds it with `find_package(CURL QUIET)`; without it, the project still builds and only `referee_scheduler_io` + the OSRM test are skipped.
- Manual network check (not in CTest): `g++ ... src/io/osrm.cpp src/algorithms/*.cpp examples/osrm_smoke.cpp ...` and run it from the repo root; it writes the cache in `data/generated/`.

## Architecture

- Public headers in `include/referee_scheduler/{domain,algorithms,io}/`; implementations in matching `src/`; entrypoint `src/main.cpp`.
- `referee_scheduler_domain` is an `INTERFACE` (header-only) CMake target. Domain entities have no `.cpp`; do not add one.
- `referee_scheduler_algorithms` is a **`STATIC` target** (it stopped being `INTERFACE` when the static-data construction landed). It compiles `src/algorithms/*.cpp` and links `referee_scheduler_domain` PUBLIC. Header declarations stay in `include/`; logic goes in `src/`. Link it from anything that includes `algorithms/`.
- `referee_scheduler_io` is a **`STATIC` target** (exists only if `find_package(CURL)` succeeds) that compiles `src/io/osrm.cpp`, links `referee_scheduler_algorithms` PUBLIC and `CURL::libcurl` PRIVATE. `algorithms/` never depends on `io/`: the dependency is inverted through `algorithms::ProveedorDistancias`, and the client implements it.
- `examples/osrm_smoke.cpp` is a manual network check (`referee_scheduler_osrm_smoke`), not a CTest test.
- All domain types are in `namespace referee_scheduler::domain`. Includes use the `referee_scheduler/domain/x.hpp` prefix (`-Iinclude` adds the root).
- Search/algorithm types live in `namespace referee_scheduler::algorithms` (`include/referee_scheduler/algorithms/`). They must not depend on `domain::Estado`.

## Conventions (differ from C++ defaults)

- Domain entities are `class` with **public fields**, no getters/setters, and default member initializers (`int id = 0;`). Data will be assigned by the future IO loader; do not reintroduce encapsulation/accessors unless asked.
- Spanish naming throughout: types `Equipo, Sede, Arbitro, Partido, Plaza, Asignacion, Estado`; fields like `sede_id`, `plaza_id`, `equipo_local_id`.
- Cross-entity references use `int` ids, not nested objects or pointers.
- A `Partido` requires one or more `Plaza` (one per role in `enum class Rol`). `nivel_requerido` and `retribucion` live on `Plaza`, not `Partido`. `Asignacion` is the pair (`plaza_id`, `arbitro_id`).
- `Arbitro` declares `disponibilidad_inicio`/`disponibilidad_fin` (no separate max-hours cap) and `domicilio_id`, the id of his **real home location**. Referee location/time is derived from the last assigned match's `sede_id`/`final`.

## Search state (decided)

- The search state is `algorithms::Estado` in `include/referee_scheduler/algorithms/estado.hpp`: `std::array<EstadoArbitro, kMaxArbitros>`, 2400 B, trivially copyable, no allocations. It is 4 B per referee, so 16 referees fit in a 64 B cache line and the full state spans 38 lines. `Estado` is the **maximum-capacity view** (initial state, debug, tests); the search nodes are stored by `algorithms::Arena`.
- `EstadoArbitro` is exactly 4 B: `sede_id : 10`, `minuto_liberacion : 11`, `reservado : 11` (10 + 11 + 11 = 32). `static_assert` enforces `sizeof(EstadoArbitro) == 4`, `sizeof(Estado) == 2400` and trivial copyability. The bitfield layout is implementation-defined; the size assertion is the guarantee. Do not add fields without redoing the bit budget.
- The state is the **minimal sufficient statistic** of the past: where each referee is, when he becomes free, plus 11 reserved bits. It stores no history (teams already refereed, match counts, etc.). Any per-referee rule needing history does not fit in 32 bits.
- `minuto_liberacion` is an absolute minute from the day's `t0`. A day is 1440 min, so 11 bits (2047) suffice with margin.
- `sede_id` has no sentinel: the initial state sets each referee at his **domicile** (`Arbitro::domicilio_id`) and free from `disponibilidad_inicio`. Match sedes and referee domiciles are **disjoint location sets**, and no assignment ever moves a referee to his domicile, so `sede_id == domicilio` identifies the initial state exactly. That is what lets `coste_asignar` treat the first trip differently without storing any history. Phase 3 asserts the disjunction.
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
- Assignment cost has **two regimes**, distinguished by `EstadoArbitro::sede_id == domicilio` (only the initial state is at the domicile):
  - **At home (first trip of the day)**: `minuto_liberacion` holds `primer_inicio`, the earliest minute he can **referee** (not the departure time). He may leave home earlier, so the transition is feasible iff `minuto_inicio_plaza >= minuto_liberacion`, regardless of travel time. He departs at `inicio − viaje`, so there is no wait: `ε + tiempo_viaje · k + (retorno_nuevo − 0) · k`. The trip is still charged as consumed time.
  - **Already worked**: `llegada = minuto_liberacion + tiempo_viaje`; infeasible if `llegada > minuto_inicio_plaza`. Cost `ε + (tiempo_viaje + espera) · k + (retorno_nuevo − retorno_anterior) · k`, with `espera = inicio − llegada` charged in full (he is no longer at home).
  - The return adjustment is prospective and telescopes, so a chain of assignments pays one real return trip at the end and every edge stays `>= ε` with a metric matrix (Dijkstra-safe).
- An infeasible transition returns `nullopt` (the generator must not create that child): starting before availability in the first regime, arriving late in the second. Negative waits never cheapen a route.
- Covered plazas do **not** add their retribution: the objective charges only time. Leaving a plaza empty with `Rol::Principal` is forbidden (`nullopt`); any secondary role costs its full retribution in cents (no epsilon).
- Future idea, **not implemented**: when the gap allows `ida + vuelta + 1 h en casa`, evaluate the same assignment "via home" (`sede = domicilio`, `liberacion += d(actual, domicilio)`) and keep the cheaper of the direct and via-home costs, so long gaps are charged as travel instead of dead time. Detailed in `docs/mejoras-practicas.md`; it needs the travel matrix and the transition generator.
- Tests: `tests/test_objetivo.cpp` covers cents conversion, the first trip from the domicile (leaving before availability), en-route wait, positive/negative return adjustment, late arrival, custom k/ε and empty plazas.

## Static data (decided; phases 1–5 implemented)

The design is five sequential phases that prepare the structures for the search. Phases 1–5 are implemented in memory; the file loader (step 5g) is pending.

- **Input aggregate**: `domain::Instancia` (`domain/instancia.hpp`) holds `equipos`, `sedes`, `arbitros`, `partidos`, `plazas`. Header-only, public fields, references by global `int` id.
- **`Sede` has `latitud`/`longitud`** (WGS84, degrees). Travel times come from the **OSRM** `/table` API, not from a matrix stored in the instance. Both coordinates 0 means "no real coordinates". A `Sede` is a **location**: either a match venue or a referee's home (`Arbitro::domicilio_id`), never both. The loader creates a separate row for a home even if it sits at the same coordinates as a venue; the two id sets are disjoint and phase 3 asserts it.
- **Time utilities** (`algorithms/tiempo.hpp` + `src/algorithms/tiempo.cpp`): `DiaCivil`, `dia_de(time_t)` (local time via `localtime_r`), `minuto_del_dia`, `indice_dia` (Howard Hinnant civil-days, no `time_t/86400`), `minuto_absoluto` (`indice_dia*1440 + minuto`) and `minuto_en_dia` (clamped to `[0, 1440]`). This is the only timezone-dependent code. `kMinutosPorDia = 1440`. **`t0` is 00:00 of the day**, so minutes are `hour*60 + minute` and a night match may exceed 1440 but stays below 2048.
- **Phase 1, partition by day** (`GrupoDia`, `agrupar_por_dia`): groups `partidos` by the **civil date of `inicio`** (never `final`), returns days sorted and, inside a day, positions in input order. A Friday 23:30 match ending Saturday 01:00 belongs to Friday. Indices are `std::size_t` positions in `Instancia::partidos`, no copies.
- **Phase 2, referee compaction** (`ArbitroDia`, `compactar_arbitros`): a referee belongs to the day if his availability window (in **absolute minutes**) contains a valid **start** minute of that day (`[dia*1440, dia*1440+1439]`). Comparing already-clamped minutes would wrongly include a referee available only the previous day. `primer_inicio` is the window start clamped to `[0, 1439]` (guaranteed by the include filter) and `ultimo_inicio` the window end clamped to `[0, 1440]`, where **1440 means no limit**. Result sorted by `id_global`, so the compacted id (vector index) is deterministic; the index is `0..N_dia-1`. `equipos_vetados` keeps only teams playing that day, sorted and deduplicated (binary search later).
- **Availability semantics (user-decided)**: the declared end is a bound on the **match start**, not on the match end. Available until 23:00 ⇒ a match starting at 23:00 is allowed even if it ends after midnight. The declared start (`primer_inicio`) is the earliest minute he can **referee**, not the earliest departure: available from 9:00 he can serve a 9:00 match even if the trip takes two hours, because he leaves home at 7:00. The first trip is still charged as travel time.
- **Phase 3, distance matrix — implemented**: `algorithms::ProveedorDistancias` (`algorithms/proveedor_distancias.hpp`) defines `Coordenada`, `Minutos = uint16_t`, `kMinutosInalcanzable = 65534` and `kMinutosViajeMinimo = 1`. `construir_matriz_distancias` compacts the day's locations (**match sedes ∪ referee domiciles**) to `0..M-1` sorted by global id, asserts `M <= 1024` (10-bit `sede_id`) and asserts that the two sets are **disjoint** (a match never plays at a domicile), asks the provider for the **full matrix**, forces the diagonal to 0, raises every other 0 to `kMinutosViajeMinimo` (OSRM returns 0 between very close points; no displacement is free) and optionally applies `cerrar_metricamente` (Floyd–Warshall, `uint32` sums, unreachable cells never used as bridges). It also fills `ArbitroDia::domicilio` with the compact index. `construir_instancia_dia` (phases 1–5) and `construir_instancias` exist. `OpcionesDistancias { bool cerrar_metricamente = true; }`.
  - The OSRM client is `io/osrm.hpp` + `src/io/osrm.cpp`: `build_url` uses `lon,lat` with 6 decimals; the server limit is on **table dimensions** (verified: 100 rows × 100 cols with 200 coordinates in the URL returns 200), so it batches 100 sources × 100 destinations, keeps all coordinates in the URL, omits `sources`/`destinations` for the full matrix, parses `durations` with a minimal JSON scanner (`null` → unreachable, `ceil(sec/60)`), and caches with an **FNV-1a** key in `data/generated/distancias_<hash>.txt` (gitignored). Network/HTTP errors throw `std::runtime_error`; the constructor clamps `maximo_por_peticion` to ≥ 1.
  - Tests: `test_instancia.cpp` (fake providers, asymmetric matrix, closure, unreachable, minimum 1-minute trip, compacted domiciles, 3-day pipeline) and `test_osrm.cpp` (offline: conversion, JSON parsing, URL).
- **Phase 4, chronological plazas — implemented**: `construir_plazas(instancia, grupo, sede_global)` expands the day's matches into `PlazaDia` records sorted by `(minuto_inicio, sede, partido_id, plaza_id)`; the index in `InstanciaDia::plazas` **is the tree level**. `PlazaDia` (48 B, trivially copyable) carries `minuto_inicio`, `minuto_fin`, the compact `sede` (`uint16_t`), `nivel_requerido`, `retribucion_centimos` (`a_centimos`, converted once), `rol`, `partido_id`, `plaza_id` and both team ids; `es_principal()` is the `rol == Rol::Principal` test, so the role is stored instead of a duplicated bool. Decisions closed here: (a) the tie-break is `(minuto_inicio, compact sede, partido_id, plaza_id)` — total and deterministic, and the compact sede orders like the global id; (b) `minuto_fin` is the **end of the match**, measured in absolute minutes from the day's 00:00, so a Friday 23:30→01:00 match ends at **1500** (`minuto_en_dia` would clamp it to 1440) and it is exactly the referee's release minute. Per-match data (minutes, compact sede) is computed once, not once per plaza; plazas whose match is not in the day are skipped. `InstanciaDia` exposes `plazas` and `n_plazas()`; `construir_instancia_dia` chains phases 1–5.
- **Phase 5, per-plaza candidate lists — implemented**: `construir_candidatos(const InstanciaDia &)` returns `ListasCandidatos{offsets, candidatos}`, a flat CSR that `construir_instancia_dia` moves into `InstanciaDia::offset_candidatos` (`n_plazas()+1` boundaries) and `InstanciaDia::candidatos`. One `plaza × árbitro` pass with four static filters, cheapest first: `nivel >= nivel_requerido`; `primer_inicio <= minuto_inicio <= ultimo_inicio` (**no** travel added to `primer_inicio`: the first trip may start before availability, and this is exactly `coste_asignar`'s at-home feasibility test); neither match team in `equipos_vetados` (sorted, `std::binary_search`); and **reachability** `d(domicilio, sede) != kMinutosInalcanzable` (a route must exist; its length is irrelevant here). Values are **compacted** referee ids (`0..n_arbitros-1`), ascending inside each plaza (deterministic tie-break), so a `uint32_t` is enough. Accessors: `n_candidatos(p)`, `candidatos_de(p)` (returns `std::span<const uint32_t>`, so the tail of the next plaza is unreachable) and `n_candidatos_totales()` (no duplicated field). A plaza with no candidates keeps both boundaries equal; a day without referees gives `{0, 0}` per plaza. The list is **necessary, not sufficient**: whether the referee arrives in time depends on the state and is decided by the child generator.
- Tests: `tests/test_instancia.cpp` covers phases 1–5 (and the time utilities); it must grow one block per phase.

## Roadmap / scope

Agreed order:

1. **Arena** for state storage: stable block storage; block size `24 + 4·n_arbitros` computed at run time; the arena receives `n_arbitros`, it does not depend on the loader. **v1 implemented** (`algorithms/arena.hpp`); v2 micro-optimizations deferred to `docs/optimizaciones.md`.
2. **Objective function**: additive and positive, in integer cents; `ε + tiempo · k` with k = 14 c/min, return-trip adjustment, and empty secondary plazas charged their lost retribution. **Implemented** (`algorithms/objetivo.hpp`); the travel lookup is filled in step 5.
3. **State transitions and operations**: the 4-byte write per assignment; decision fields of the node.
4. **Search frontier / open list**.
5. **Static data**:
   a. In-memory instance (`n_arbitros`, `t0`, referees, matches, plazas). **Implemented**: `domain::Instancia`, `domain::Sede` coordinates.
   b. Per-day referee compaction (global id -> `0..N_dia-1`) and sede remap. **Implemented**: `compactar_arbitros` and `construir_matriz_distancias`.
   c. Travel-time matrix. **Implemented**: OSRM `/table` via `io::OsrmDistancias`, with cache.
   d. Minute offsets for match start/end. **Implemented**: `tiempo.hpp` with `t0 = 00:00`.
   e. Chronological ordering of plazas. **Implemented**: `construir_plazas` / `PlazaDia` (phase 4).
   f. Per-plaza candidate lists (CSR + reachability). **Implemented**: `construir_candidatos` / `ListasCandidatos` (phase 5).
   g. Instance loader / file format (`data/instances/`). **Pending** (step 5g, after the phases).
6. **Assemble Dijkstra / A\***.
7. **Final validation**: recompute constraints and cost from the reconstructed assignments.

## Known gaps / pending decisions

- Phase 4 is closed: the plaza order is `(minuto_inicio, compact sede, partido_id, plaza_id)` and `PlazaDia::minuto_fin` is the match end in absolute minutes of the day (can exceed 1440). It is exactly the referee's release minute, so the transition generator must write it into `EstadoArbitro::minuto_liberacion`. A plaza whose `partido_id` does not exist is silently ignored by `construir_plazas`; validating referential integrity is the loader's job.
- Phase 5 is closed: candidate ids are **compacted** referee ids (`uint32_t`, ascending per plaza) and the static filters deliberately exclude travel time (only route existence). The phase-2 inclusion filter is a **superset** (a referee whose window touches the day but no real match start can end up with empty lists); that is intentional and costs only state bytes. A plaza with an empty list can only be left empty: the child generator must not invent candidates, and `coste_asignar` still enforces feasibility and the principal-plaza rule.
- The public OSRM demo server is for demos and has limits/rate limits. `OpcionesOsrm::url` is configurable for a local Docker instance; do not hardcode the demo URL anywhere else. The cache key includes URL + profile + coordinates.
- `Sede` coordinates are required: a sede with both at 0 triggers an `assert` in `construir_matriz_distancias`.
- `Arbitro::disponibilidad_*` are `time_t`; the conversion is done in `tiempo.hpp`. The declared start (`primer_inicio`) is the earliest **match start** he can serve; the first trip may leave home before it, so no per-referee departure field is stored. That regime is detected with `sede_id == domicilio`, which is only possible in the initial state because match sedes and domiciles are disjoint.
- If the "via home" idea is implemented, the referee would legitimately be at his domicile **after** having worked, so `sede_id == domicilio` would no longer identify the initial state. That change needs a marker (a reserved bit or equivalent) before it can land.
- `kMaxArbitros = 600` is a hard cap; if the real instance exceeds it, that is a bug. The 10-bit `sede_id` requires `M_dia <= 1024`, guaranteed by per-day sede compaction.
- Node budget / memory limit, tie-breaking/determinism, and the parallel mechanism for the three days are undefined.
- Tests cover smoke, arena, objective, static-data phases 1–5 and the OSRM client (offline); grow them as the loader lands.
- No benchmark or baseline yet; `docs/optimizaciones.md` proposes a minimal arena benchmark to be added once the search runs.

## Rules

- Always add the necessary comments for the user to understand the newly created or modified code.
- Wait until the user has approved all the changes. Once approved, proactively ask if I should add exhaustive comments and documentation across all the modified code.
  -Only whenn I asked you for the commit message you will have to update the context
