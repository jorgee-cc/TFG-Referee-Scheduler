# AGENTS.md

Academic C++20 project (TFG): search algorithms for the Referee Assignment Problem (RAP).
Goal: maximize retribution per hour worked by reducing travel and idle ("tiempos muertos") time.
Most directories are still placeholders; the only real code is the domain model and `src/main.cpp`.

## Build & verify

- Intended build (README): `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- `cmake` is NOT installed in this environment. Verify manually instead:
  - `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude src/main.cpp -o /tmp/opencode/referee_scheduler`
  - Header-only checks: compile a temp `.cpp` that includes the headers.
- `tests/CMakeLists.txt` declares `test_smoke.cpp`, which does not exist. The default configure (`BUILD_TESTING=ON`) fails; use `-DBUILD_TESTING=OFF` or create the file.

## Architecture

- Public headers in `include/referee_scheduler/{domain,algorithms,io}/`; implementations in matching `src/`; entrypoint `src/main.cpp`.
- `referee_scheduler_domain` is an `INTERFACE` (header-only) CMake target. Domain entities have no `.cpp`; do not add one.
- All domain types are in `namespace referee_scheduler::domain`. Includes use the `referee_scheduler/domain/x.hpp` prefix (`-Iinclude` adds the root).

## Conventions (differ from C++ defaults)

- Domain entities are `class` with **public fields**, no getters/setters, and default member initializers (`int id = 0;`). Data will be assigned by the future IO loader; do not reintroduce encapsulation/accessors unless asked.
- Spanish naming throughout: types `Equipo, Sede, Arbitro, Partido, Plaza, Asignacion, Estado`; fields like `sede_id`, `plaza_id`, `equipo_local_id`.
- Cross-entity references use `int` ids, not nested objects or pointers.
- `Estado` (`estado.hpp`) is the search-space node. It holds `std::vector<Asignacion>` plus two derived caches indexed by referee id: `ultima_sede_arbitro` (current venue; `-1` unset) and `franja_ocupada_arbitro` (occupied time slot; `-1` free). These caches are redundant with `asignaciones` and MUST be kept in sync on every successor; initialize them (size = nº referees) when building the initial state, since `Estado` cannot see the instance. The old `enum class Estado` lifecycle enum was removed; do not restore it.
- The `franja` (time slot) concept is not yet modeled in the domain; `franja_ocupada_arbitro` stores an int slot id whose source still needs defining.
- A `Partido` requires one or more `Plaza` (one per role in `enum class Rol`). `nivel_requerido` and `retribucion` live on `Plaza`, not `Partido`. `Asignacion` is the pair (`plaza_id`, `arbitro_id`).
- `Arbitro` declares `disponibilidad_inicio`/`disponibilidad_fin` (no separate max-hours cap). Referee location/time is derived from the last assigned match's `sede_id`/`final`.

## Roadmap / scope

- `docs/Apuntes.md` holds the phased plan and deferred optimization notes. Follow the order: (1) state space, (2) graph construction, (3) objective function + heuristic, (4) optimization, (5) data loading/IO.
- Do not build IO or optimize ahead of the current phase unless explicitly asked.

## Rules

- Always add the necessary comments for the user to understand the newly created or modified code.
- Wait until the user has approved all the changes. Once approved, proactively ask if you should add exhaustive comments and documentation across all the modified code.
  -Only whenn I asked you for the commit message you will have to update the context
