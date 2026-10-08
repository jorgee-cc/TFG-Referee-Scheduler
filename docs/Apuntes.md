# Apuntes

Notas de trabajo y decisiones aplazadas para el TFG (Referee Scheduler / Referee Assignment Problem).

## Roadmap

1. **Espacio de estados** (actual): representación del nodo del grafo de búsqueda.
   - `Estado` contiene `std::vector<Asignacion>`; estado inicial vacío.
2. **Construcción del grafo**: operadores/sucesores, generación de vecinos, factibilidad.
3. **Función objetivo y heurística**: maximizar la retribución por hora trabajada.
   - Incluye tiempo efectivo de arbitraje, tiempo de desplazamiento entre sedes y tiempos muertos.
4. **Optimización**: rendimiento, estructuras de datos, búsqueda.
5. **Carga de datos (IO)**: lectura de instancias y volcado de resultados.

## Decisiones de diseño actuales

- Clases del dominio con campos públicos, sin getters/setters, definidas *header-only*.
- `std::time_t` para los instantes de `Partido`.
- Referencias entre entidades mediante `id` (`sede_id`, `equipo_local_id`, ...).
- `Asignacion` como par (`partido_id`, `arbitro_id`); sin enum de ciclo de vida.

## Ideas de optimización (aplazadas)

- Agregado `Instancia` que posea los vectores contiguos de `Equipo`, `Sede`, `Arbitro` y `Partido`.
- Matriz plana (row-major) de tiempos de viaje entre sedes, precalculada al cargar la instancia.
- Vetos como bitmask (`std::vector<uint64_t>` o `std::bitset`) para factibilidad O(1).
- Referencias por índice posicional en lugar de por `id`, con mapeo id -> índice en la carga.
- Structs/trivialmente copiables para permitir `memcpy` y mejorar localidad de caché.
