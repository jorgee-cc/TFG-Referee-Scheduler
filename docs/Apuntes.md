# Apuntes

Notas de trabajo y decisiones aplazadas para el TFG (Referee Scheduler / Referee Assignment Problem).

## Roadmap

1. **Espacio de estados** (actual): representación del nodo del grafo de búsqueda.
   - `Estado` contiene `std::vector<Asignacion>` más las cachés `ultima_sede_arbitro` y `franja_ocupada_arbitro`.
2. **Construcción del grafo**: operadores/sucesores, generación de vecinos, factibilidad.
3. **Función objetivo y heurística**: maximizar la retribución por hora trabajada.
   - Incluye tiempo efectivo de arbitraje, tiempo de desplazamiento entre sedes y tiempos muertos.
4. **Optimización**: rendimiento, estructuras de datos, búsqueda.
5. **Carga de datos (IO)**: lectura de instancias y volcado de resultados.

## Decisiones de diseño actuales

- Clases del dominio con campos públicos, sin getters/setters, definidas *header-only*.
- `std::time_t` para los instantes de `Partido` y para la disponibilidad de `Arbitro`.
- Referencias entre entidades mediante `id` (`sede_id`, `partido_id`, `plaza_id`, ...).
- **Plazas y roles**: un partido exige cubrir varias `Plaza` (una por rol: principal, asistente,
  anotador, juez de línea). El nivel requerido y la retribución pertenecen a la `Plaza`, no al
  `Partido`, porque dependen del rol.
- `Asignacion` es el par (`plaza_id`, `arbitro_id`): cubre una plaza concreta, no un partido entero.
- **Disponibilidad**: cada `Arbitro` declara `disponibilidad_inicio` y `disponibilidad_fin`. No hay
  un tope de horas trabajadas independiente de esa ventana.
- **El estado tiene cachés derivadas**: además de `asignaciones`, guarda:
  - `ultima_sede_arbitro` (índice = id de árbitro, valor = id de su sede actual; `-1` si no tiene);
  - `franja_ocupada_arbitro` (índice = id de árbitro, valor = id de la franja ocupada; `-1` si libre).
  - Son información **redundante** que se puede derivar de `asignaciones`, pero se cachea para pasar
    de O(nº asignaciones) a O(1) en la comprobación de factibilidad. Deben mantenerse sincronizadas
    en cada sucesor y dimensionarse (nº de árbitros) al construir el estado inicial.
  - Requiere ids de árbitro densos y desde 0, o un mapeo id -> índice en la carga.
- Resto de datos derivados durante la factibilidad (no se almacenan):
  - disponibilidad = `final` del último partido + viaje de vuelta al domicilio <= `disponibilidad_fin`;
  - nivel = `nivel_requerido` de la plaza; retribución = `retribucion` de la plaza.

## Conceptos pendientes de modelar

- **Franja horaria**: no existe una entidad `Franja` en el dominio; falta definir cómo se obtiene el
  id de franja a partir de `Partido.inicio`/`Partido.final` (o usar el índice del partido como franja).

## Ideas de optimización (aplazadas)

- Agregado `Instancia` que posea los vectores contiguos de `Equipo`, `Sede`, `Arbitro`, `Partido` y `Plaza`.
- Matriz plana (row-major) de tiempos de viaje entre sedes, precalculada al cargar la instancia.
- Vetos como bitmask (`std::vector<uint64_t>` o `std::bitset`) para factibilidad O(1).
- Referencias por índice posicional en lugar de por `id`, con mapeo id -> índice en la carga.
- Structs/trivialmente copiables para permitir `memcpy` y mejorar localidad de caché.
