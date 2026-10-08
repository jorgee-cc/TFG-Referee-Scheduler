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
- Spanish naming throughout: types `Equipo, Sede, Arbitro, Partido, Asignacion, Estado`; fields like `sede_id`, `equipo_local_id`.
- Do NOT write code comments (project preference).
- Cross-entity references use `int` ids, not nested objects or pointers.
- `Estado` (`estado.hpp`) is the search-space node and holds `std::vector<Asignacion>`. The old `enum class Estado` lifecycle enum was removed; do not restore it.

## Roadmap / scope

- `docs/Apuntes.md` holds the phased plan and deferred optimization notes. Follow the order: (1) state space, (2) graph construction, (3) objective function + heuristic, (4) optimization, (5) data loading/IO.
- Do not build IO or optimize ahead of the current phase unless explicitly asked.

## Rules

- Always add the necessary comments for the user to understand the newly created or modified code.
- Wait until the user has approved all the changes. Once approved, proactively ask if you should add exhaustive comments and documentation across all the modified code.
