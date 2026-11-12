# AGENTS.md

Academic C++20 project (TFG): search algorithms for the Referee Assignment Problem (RAP).
Goal: maximize retribution per hour worked by reducing travel and idle ("tiempos muertos") time.
The domain model, `src/main.cpp` and the packed search state (`algorithms/estado.hpp`) are the only real code so far.

## Build & verify

- Intended build (README): `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- `cmake` is NOT installed in this environment. Verify manually instead:
  - Main: `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude src/main.cpp -o /tmp/opencode/referee_scheduler`
  - Tests: `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude tests/test_smoke.cpp -o /tmp/opencode/referee_scheduler_tests && /tmp/opencode/referee_scheduler_tests`
  - Header-only checks: compile a temp `.cpp` that includes the headers.
- `tests/CMakeLists.txt` points to `tests/test_smoke.cpp`, which now exists. Keep them in sync.

## Architecture

- Public headers in `include/referee_scheduler/{domain,algorithms,io}/`; implementations in matching `src/`; entrypoint `src/main.cpp`.
- `referee_scheduler_domain` is an `INTERFACE` (header-only) CMake target. Domain entities have no `.cpp`; do not add one.
- `referee_scheduler_algorithms` is an `INTERFACE` target that links `referee_scheduler_domain`. Link it from anything that includes `algorithms/` or `io/`.
- All domain types are in `namespace referee_scheduler::domain`. Includes use the `referee_scheduler/domain/x.hpp` prefix (`-Iinclude` adds the root).
- Search/algorithm types live in `namespace referee_scheduler::algorithms` (`include/referee_scheduler/algorithms/`). They must not depend on `domain::Estado`.

## Conventions (differ from C++ defaults)

- Domain entities are `class` with **public fields**, no getters/setters, and default member initializers (`int id = 0;`). Data will be assigned by the future IO loader; do not reintroduce encapsulation/accessors unless asked.
- Spanish naming throughout: types `Equipo, Sede, Arbitro, Partido, Plaza, Asignacion, Estado`; fields like `sede_id`, `plaza_id`, `equipo_local_id`.
- Cross-entity references use `int` ids, not nested objects or pointers.
- A `Partido` requires one or more `Plaza` (one per role in `enum class Rol`). `nivel_requerido` and `retribucion` live on `Plaza`, not `Partido`. `Asignacion` is the pair (`plaza_id`, `arbitro_id`).
- `Arbitro` declares `disponibilidad_inicio`/`disponibilidad_fin` (no separate max-hours cap). Referee location/time is derived from the last assigned match's `sede_id`/`final`.

## Search state (decided)

- The search state is `algorithms::Estado` in `include/referee_scheduler/algorithms/estado.hpp`: `std::array<EstadoArbitro, kMaxArbitros>`, 1200 B, trivially copyable, no allocations. It is 4 B per referee, so 16 referees fit in a 64 B cache line and the full state spans 19 lines.
- `EstadoArbitro` is exactly 4 B: `sede_id : 10`, `minuto_liberacion : 11`, `reservado : 11` (10 + 11 + 11 = 32). `static_assert` enforces `sizeof(EstadoArbitro) == 4`, `sizeof(Estado) == 1200` and trivial copyability. The bitfield layout is implementation-defined; the size assertion is the guarantee. Do not add fields without redoing the bit budget.
- The state is the **minimal sufficient statistic** of the past: where each referee is, when he becomes free, plus 11 reserved bits. It stores no history (teams already refereed, match counts, etc.). Any per-referee rule needing history does not fit in 32 bits.
- `minuto_liberacion` is an absolute minute from the day's `t0`. A day is 1440 min, so 11 bits (2047) suffice with margin.
- `sede_id` has no sentinel: the initial state sets each referee at his domicile (`Arbitro::sede_id`) and free from `disponibilidad_inicio`.
- `reservado` is unused by design. There is **no per-referee retribution cap**; the field is a bit budget for a future constraint and can be subdivided without changing `sizeof`.
- `kMaxArbitros = 300` is a compile-time **capacity**, not a count. The real count `n_arbitros` is runtime data (in the instance). Only `[0, n_arbitros)` is meaningful. Never loop to `kMaxArbitros`. Assert `n_arbitros <= kMaxArbitros` on load: unused slots are zero, and a zero slot looks like a valid referee (sede 0, minute 0).
- Execution is **per day**: Friday, Saturday and Sunday are independent and run in parallel. Each day builds its own instance and state. Per-day **referee compaction** is the agreed direction: `Estado` indexes the day's available referees `0..N_dia-1`, not global ids. Optional per-day **sede compaction** only frees bits (10 -> 6/7), not bytes.
- `domain::Estado` (`vector<Asignacion>` + `ultima_sede_arbitro` + `franja_ocupada_arbitro`) is **legacy and superseded** by `algorithms::Estado`. Do not extend it. Its fate (delete, or keep as the solution type) is undecided; ask before changing it. The `franja` concept is superseded by continuous minutes.

## Roadmap / scope

Agreed order:

1. **Arena** for state storage: stable block storage; block size `24 + 4·n_arbitros` computed at run time; the arena receives `n_arbitros`, it does not depend on the loader.
2. **Objective function**: additive and positive; the user sets its parameter. Define the formula first; the travel lookup is filled in step 5.
3. **State transitions and operations**: the 4-byte write per assignment; decision fields of the node.
4. **Search frontier / open list**.
5. **Static data**:
   a. In-memory instance (`n_arbitros`, `t0`, referees, matches, plazas).
   b. Per-day referee compaction (global id -> `0..N_dia-1`); optional sede remap.
   c. Per-plaza candidate lists.
   d. Travel-time matrix (or sede coordinates).
   e. Minute offsets for match start/end.
   f. Instance loader / file format (`data/instances/`).
6. **Assemble Dijkstra / A\***.
7. **Final validation**: recompute constraints and cost from the reconstructed assignments.

## Known gaps / pending decisions

- `Sede` has no coordinates and there is no travel-time matrix yet; the travel cost cannot be computed until this is decided (matrix in the instance vs coordinates + formula). Scheduled in step 5.
- Unfilled plazas policy is undecided (skip transition with penalty?).
- Idle time before the first match / after the last match is undecided.
- `Plaza::retribucion` is `double`; the cost representation (cents vs double) is undecided.
- `Arbitro::disponibilidad_*` are `time_t`; `t0` and the minute conversion (with clamping/validation) are pending.
- `kMaxArbitros = 300` is a hard cap; if the real instance exceeds it, that is a bug.
- Node budget / memory limit, tie-breaking/determinism, and the parallel mechanism for the three days are undefined.
- Tests are a single smoke test for now; grow them as the arena/objective/transitions land.
- No benchmark or baseline (decided not needed for now).

## Rules

- Always add the necessary comments for the user to understand the newly created or modified code.
- Wait until the user has approved all the changes. Once approved, proactively ask if I should add exhaustive comments and documentation across all the modified code.
  -Only whenn I asked you for the commit message you will have to update the context
