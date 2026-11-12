# Apuntes

Notas de trabajo y decisiones aplazadas para el TFG (Referee Scheduler / Referee Assignment Problem).

## Roadmap

El orden de trabajo operativo vive en `AGENTS.md`: arena, función objetivo, transiciones del
estado, frontera de búsqueda, datos estáticos, ensamblado de Dijkstra y validación final.

## Decisiones de diseño actuales

- Clases del dominio con campos públicos, sin getters/setters, definidas _header-only_.
- `std::time_t` para los instantes de `Partido` y para la disponibilidad de `Arbitro`.
- Referencias entre entidades mediante `id` (`sede_id`, `partido_id`, `plaza_id`, ...).
- **Plazas y roles**: un partido exige cubrir varias `Plaza` (una por rol: principal, asistente,
  anotador, juez de línea). El nivel requerido y la retribución pertenecen a la `Plaza`, no al
  `Partido`, porque dependen del rol.
- `Asignacion` es el par (`plaza_id`, `arbitro_id`): cubre una plaza concreta, no un partido entero.
- **Disponibilidad**: cada `Arbitro` declara `disponibilidad_inicio` y `disponibilidad_fin`. No hay
  un tope de horas trabajadas independiente de esa ventana.
- **El estado de búsqueda** vive en `algorithms::Estado` (`include/referee_scheduler/algorithms/estado.hpp`):
  - `EstadoArbitro` ocupa 4 B: `sede_id` (10 bits), `minuto_liberacion` (11 bits) y `reservado`
    (11 bits). 10 + 11 + 11 = 32; `static_assert` fija `sizeof(EstadoArbitro) == 4`.
  - `Estado` es `std::array<EstadoArbitro, 300>` = 1200 B, trivialmente copiable y sin reservas
    dinámicas. 16 árbitros caben en una línea de caché de 64 B y el estado completo ocupa 19 líneas.
  - Es la **mínima estadística suficiente** del pasado: dónde está cada árbitro, cuándo queda libre
    y 11 bits reservados. No guarda historial (equipos ya arbitrados, número de partidos, etc.).
  - `minuto_liberacion` es un minuto absoluto desde el `t0` del día; un día (1440 min) cabe en 11 bits.
  - La sede no tiene centinela: el estado inicial sitúa a cada árbitro en su domicilio y libre desde
    `disponibilidad_inicio`.
  - `kMaxArbitros = 300` es **capacidad de compilación**, no el número real. `n_arbitros` es de
    ejecución y solo se usa `[0, n_arbitros)`. Los huecos sin usar son ceros y no deben recorrerse:
    un hueco a cero parece un árbitro válido (sede 0, minuto 0).
  - La ejecución es **por días** (viernes, sábado y domingo independientes y en paralelo). Se acordó
    **compactar árbitros por día**: el estado indexa `0..N_dia-1`, no los ids globales.
  - `domain::Estado` con `asignaciones`, `ultima_sede_arbitro` y `franja_ocupada_arbitro` queda
    **obsoleto** y pendiente de decidir si se elimina o se conserva como tipo solución.

## Conceptos pendientes de modelar

- **Tiempo de viaje**: `Sede` no tiene coordenadas ni existe una matriz de viaje. Hay que decidir si
  la instancia la trae o se calcula a partir de coordenadas.
- **Franja horaria**: el estado continuo (`minuto_liberacion`) la sustituye; no se modela como entidad.
- **Plazas sin cubrir**: falta decidir si se permiten y con qué penalización.
- **Tiempo muerto**: falta decidir si cuenta antes del primer partido y después del último.
- **Representación del coste**: `Plaza::retribucion` es `double`; decidir céntimos frente a coma
  flotante para que las comparaciones sean exactas.

## Ideas de optimización (aplazadas)

- Agregado `Instancia` que posea los vectores contiguos de `Equipo`, `Sede`, `Arbitro`, `Partido` y
  `Plaza`.
- Matriz plana (row-major) de tiempos de viaje entre sedes, precalculada al cargar la instancia.
- Vetos como bitmask (`std::vector<uint64_t>` o `std::bitset`) para factibilidad O(1).
- Referencias por índice posicional en lugar de por `id`, con mapeo id -> índice en la carga.
- Arena por bloques para los estados, con punteros estables.
- Copiar solo `4 · N_dia` bytes del estado cuando se compacta por día.
- Structs/trivialmente copiables para permitir `memcpy` y mejorar localidad de caché. _Adoptado en
  `algorithms::Estado`._
