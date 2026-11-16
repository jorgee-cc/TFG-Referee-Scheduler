# Apuntes

Notas de trabajo y decisiones aplazadas para el TFG (Referee Scheduler / Referee Assignment Problem).

## Roadmap

El orden de trabajo operativo vive en `AGENTS.md`: arena, función objetivo, transiciones del
estado, frontera de búsqueda, datos estáticos, ensamblado de Dijkstra y validación final.

## Decisiones de diseño actuales

- Clases del dominio con campos públicos, sin getters/setters, definidas _header-only_.
- `std::time_t` para los instantes de `Partido` y para la disponibilidad de `Arbitro`.
- Referencias entre entidades mediante `id` (`sede_id`, `domicilio_id`, `partido_id`, `plaza_id`, ...).
- **Plazas y roles**: un partido exige cubrir varias `Plaza` (una por rol: principal, asistente,
  anotador, juez de línea). El nivel requerido y la retribución pertenecen a la `Plaza`, no al
  `Partido`, porque dependen del rol.
- `Asignacion` es el par (`plaza_id`, `arbitro_id`): cubre una plaza concreta, no un partido entero.
- **Disponibilidad**: cada `Arbitro` declara `disponibilidad_inicio` y `disponibilidad_fin`. No hay
  un tope de horas trabajadas independiente de esa ventana. El inicio es el primer minuto en que
  puede **arbitrar** (empezar un partido), no la hora de salida: puede salir de casa antes.
- **Domicilio**: `Arbitro::domicilio_id` apunta a una `Sede` que es su casa real y que **ningún
  partido usa como sede**. Las sedes de partido y los domicilios son conjuntos disjuntos (la fase 3
  lo exige con un `assert`). Una `Sede` es una ubicación: o recinto de partidos o domicilio, nunca
  las dos cosas; si una casa está en el mismo punto que un recinto, son dos filas distintas.
- **El estado de búsqueda** vive en `algorithms::Estado` (`include/referee_scheduler/algorithms/estado.hpp`):
  - `EstadoArbitro` ocupa 4 B: `sede_id` (10 bits), `minuto_liberacion` (11 bits) y `reservado`
    (11 bits). 10 + 11 + 11 = 32; `static_assert` fija `sizeof(EstadoArbitro) == 4`.
  - `Estado` es `std::array<EstadoArbitro, 600>` = 2400 B, trivialmente copiable y sin reservas
    dinámicas. 16 árbitros caben en una línea de caché de 64 B y el estado completo ocupa 38 líneas.
  - Es la **mínima estadística suficiente** del pasado: dónde está cada árbitro, cuándo queda libre
    y 11 bits reservados. No guarda historial (equipos ya arbitrados, número de partidos, etc.).
  - `minuto_liberacion` es un minuto absoluto desde el `t0` del día; un día (1440 min) cabe en 11 bits.
  - La sede no tiene centinela: el estado inicial sitúa a cada árbitro en su **domicilio** y libre
    desde `disponibilidad_inicio`. Como ningún partido se juega en un domicilio, después de
    cualquier asignación la sede es la de un partido: `sede == domicilio` identifica el estado
    inicial, y la función objetivo distingue con eso el primer desplazamiento sin guardar historial.
    Además, en el estado inicial `minuto_liberacion` significa "primer minuto en que puede
    arbitrar", no "hora de salida".
  - `kMaxArbitros = 600` es **capacidad de compilación**, no el número real. `n_arbitros` es de
    ejecución y solo se usa `[0, n_arbitros)`. Los huecos sin usar son ceros y no deben recorrerse:
    un hueco a cero parece un árbitro válido (sede 0, minuto 0).
  - La ejecución es **por días** (viernes, sábado y domingo independientes y en paralelo). Se acordó
    **compactar árbitros por día**: el estado indexa `0..N_dia-1`, no los ids globales.
  - `domain::Estado` con `asignaciones`, `ultima_sede_arbitro` y `franja_ocupada_arbitro` queda
    **obsoleto** y pendiente de decidir si se elimina o se conserva como tipo solución.
- **La arena de búsqueda** vive en `algorithms::Arena` (`include/referee_scheduler/algorithms/arena.hpp`):
  - Cada nodo es un bloque contiguo de `24 + 4·n_arbitros` bytes: `CabeceraNodo` (24 B) + un
    `EstadoArbitro` por árbitro. El *stride* se calcula en ejecución y se redondea a 8 B.
  - `CabeceraNodo`: `coste_g` (int64), `padre`, `nivel`, `flags`, `decision`; 24 B exactos porque
    entran en la fórmula del bloque. `kSinNodo` = "sin padre". `decision` guarda `0..N-1` (árbitro
    compacto asignado), `kDecisionVacia` o `kSinDecision` (raíz); así la solución se reconstruye
    leyendo la cadena de padres, sin comparar estados.
  - `flags` queda reservado para la frontera (bits `kFlagAbierto`/`kFlagCerrado`); el generador de
    hijos lo deja a cero.
  - Recibe `n_arbitros` en el constructor (no depende del cargador ni de `Instancia`).
  - `estado(nodo)` devuelve un `std::span` de longitud `n_arbitros`: no se puede iterar el relleno.
    `crear(Estado, ...)` copia solo `[0, n_arbitros)` e ignora la cola.
  - Direcciones estables: bloques de 64 KiB en `unique_ptr`, nunca se mueven; el crecimiento duplica
    bloques y es amortizado. La arena no es copiable, sí movible.
  - Optimizaciones de hardware estudiadas (zeroing, división, hugepages/TLB): **no implementadas**,
    documentadas en `docs/optimizaciones.md` para cuando el algoritmo funcione y se pueda medir.

## Transiciones (paso 3 implementado)

`include/referee_scheduler/algorithms/transiciones.hpp` + `src/algorithms/transiciones.cpp` preparan
el árbol de búsqueda. La escritura por asignación es de 4 B: `sede_id = plaza.sede`,
`minuto_liberacion = plaza.minuto_fin`, `reservado = 0`.

- **Estado inicial** (`estado_inicial`): cada árbitro compacto queda en su domicilio
  (`sede_id = ArbitroDia::domicilio`), libre desde `primer_inicio` y con `reservado = 0`. La cola
  `[n_arbitros, kMaxArbitros)` se deja a cero. Estar en el domicilio es la marca del régimen "aún
  no ha salido de casa" de `coste_asignar`.
- **Raíz** (`crear_raiz`): exige una arena nueva (`nodos_creados() == 0`), mismo `n_arbitros` y
  `n_arbitros > 0`. Crea el nodo 0 con coste 0, `padre = kSinNodo`, `nivel = 0`, `flags = 0` y
  `decision = kSinDecision`.
- **Meta** (`es_meta`): `nivel == n_plazas()`. El nivel es el número de plazas decididas y el índice
  de la siguiente; el coste de la meta ya incluye viajes, esperas, retornos telescópicos y plazas
  vacías, sin coste terminal adicional.
- **Generación de hijos** (`generar_hijos`): dos formas que comparten `detalle::generar_hijos_impl`:
  1. con `std::vector<std::uint32_t>&` de salida (se limpia y se rellena);
  2. con callback, plantilla en cabecera, para insertar directamente en la frontera sin vector
     intermedio.
  El orden es determinista: candidatos ascendentes y después la rama vacía. Por cada candidato se
  leen tres distancias, se llama a `coste_asignar`, se copia el estado del padre y se parchea solo el
  registro del árbitro. La rama vacía copia el estado intacto y cobra `coste_dejar_vacia`; las
  principales no la tienen. Cualquier celda `kMinutosInalcanzable` descarta el hijo.
- **Factibilidad estática** (`tiene_solucion_estatica`): toda plaza principal debe tener al menos un
  candidato. Es necesaria, no suficiente: la llegada tarde depende del estado. Los días sin árbitros
  y solo secundarias son válidos (todas vacías); una principal sin candidatos es infeasible.
- **Reconstrucción** (`reconstruir_solucion`): sube de la meta a la raíz leyendo `decision`. Un nodo
  con `nivel = p + 1` decidió `plazas[p]`; devuelve `SolucionDia` con coste y `DecisionDia`
  (`plaza_id`, `partido_id`, `arbitro_global`; `-1` si quedó vacía) en orden cronológico.
- Tests: `tests/test_transiciones.cpp` cubre estado inicial, raíz, primer viaje desde casa, plaza
  vacía, principal sin candidatos, llegada tarde, factibilidad estática, centinela de vuelta,
  equivalencia callback/vector y reconstrucción exacta (982 céntimos en el caso de dos partidos).

## Datos estáticos (fases 1-5 implementadas)

La carga de la instancia se divide en cinco fases que preparan la búsqueda. De momento es
**construcción en memoria**; el cargador de fichero es el paso 5g.

- **Agregado de entrada**: `domain::Instancia` (`domain/instancia.hpp`) con los vectores de
  `Equipo`, `Sede`, `Arbitro`, `Partido` y `Plaza`. Header-only y con referencias por `id`.
- **`Sede` tiene `latitud`/`longitud`** (WGS84). Los tiempos de viaje se piden a la API `/table`
  de **OSRM**; no se guarda una matriz N×N en la instancia. Ambas coordenadas a cero = sede sin
  geolocalizar.
- **Tiempo** (`algorithms/tiempo.hpp` + `src/algorithms/tiempo.cpp`): `DiaCivil`, `dia_de`
  (hora local con `localtime_r`), `minuto_del_dia`, `indice_dia` (días civiles de Hinnant, sin
  dividir `time_t` entre 86400) y `minuto_absoluto`. `t0` es **las 00:00 del día**, así que un
  minuto es `hora*60 + minuto`. Es el único código que depende de la zona horaria.
- **Fase 1, partición por día** (`GrupoDia`, `agrupar_por_dia`): agrupa por la **fecha civil de
  `inicio`**, nunca por `final`. Un partido del viernes a las 23:30 que acaba el sábado a la 01:00
  es del viernes. Días ordenados y, dentro, posiciones de entrada.
- **Fase 2, compactación de árbitros** (`ArbitroDia`, `compactar_arbitros`): entra quien tiene
  algún minuto de **inicio** válido del día en su ventana, comparada en **minutos absolutos**
  (compararla ya recortada incluiría por error a quien solo estaba disponible el día anterior).
  `primer_inicio` es el inicio de la ventana recortado a `[0, 1439]` (lo garantiza el filtro de
  entrada) y `ultimo_inicio` el fin recortado a `[0, 1440]`, donde 1440 = sin límite. El
  resultado se ordena por `id_global`, así que el id compacto (índice) es determinista y cubre
  `0..N_dia-1`. Los vetos se recortan a los equipos del día, ordenados y sin duplicados.
- **Semántica de disponibilidad (decidida por el usuario)**: el fin declarado limita la **hora de
  inicio** del partido, no su final. Hasta las 23:00 permite empezar a las 23:00 aunque acabe
  después de medianoche. El inicio declarado (`primer_inicio`) es el primer minuto en que puede
  **arbitrar**, no la hora a la que puede salir de casa: con disponibilidad desde las 9:00 puede
  cubrir un partido a las 9:00 aunque tarde dos horas, porque sale a las 7:00. El viaje se cobra
  igual; solo cambia la factibilidad (basta con que el partido no empiece antes de `primer_inicio`).
- **Fase 3, matriz de distancias (implementada)**:
  - `algorithms::ProveedorDistancias` (interfaz) con `Coordenada`, `Minutos = uint16_t`,
    `kMinutosInalcanzable = 65534` y `kMinutosViajeMinimo = 1`.
  - `construir_matriz_distancias` compacta las ubicaciones del día (sedes de partidos ∪ domicilios
    de árbitros) a `0..M-1` por id ascendente, exige `M <= 1024` (10 bits del estado) y exige que
    ambos conjuntos sean **disjuntos**, pide la matriz completa, fuerza la diagonal a 0, eleva
    cualquier otro 0 a `kMinutosViajeMinimo` (OSRM devuelve 0 entre puntos muy cercanos y ningún
    desplazamiento es gratis) y aplica el cierre métrico si se pide. También rellena
    `ArbitroDia::domicilio` con el índice compacto.
  - La matriz es **completa y asimétrica**: OSRM da tiempos distintos por sentido (verificado), así
    que no se espeja. `ceil(segundos/60)` conserva la desigualdad triangular. `cerrar_metricamente`
    (Floyd-Warshall, sumas en `uint32`, los inalcanzables no hacen de puente) la impone por si la
    red real la incumple.
  - Cliente `io::OsrmDistancias` (`io/osrm.hpp` + `src/io/osrm.cpp`): libcurl, URL con `lon,lat` y 6
    decimales, lotes de 100×100 (el límite del servidor es de **dimensiones de tabla**, no de
    coordenadas en la URL: 200 coordenadas con tabla 100×100 funciona), `null` → inalcanzable y
    caché con hash FNV-1a en `data/generated/distancias_<hash>.txt`.
  - `construir_instancia_dia` (fases 1-5) y `construir_instancias` (una por fecha).
- **Fase 4, plazas cronológicas (implementada)**: `construir_plazas(instancia, grupo, sede_global)`
  expande los partidos del día en `PlazaDia` ordenadas por `(minuto_inicio, sede compacta, partido_id,
  plaza_id)`; el índice en `InstanciaDia::plazas` **es el nivel del árbol**. `PlazaDia` (48 B,
  trivially copyable) lleva `minuto_inicio`, `minuto_fin`, la sede compacta, `nivel_requerido`,
  `retribucion_centimos` (`a_centimos`, una sola vez), `rol` (y `es_principal()`), `partido_id`,
  `plaza_id` y los dos equipos. Decisiones cerradas: el desempate es
  `(inicio, sede compacta, partido, plaza)`, total y determinista; `minuto_fin` es el **final del
  partido** en minutos absolutos del día (23:30→01:00 vale 1500, no se recorta a 1440) y es
  exactamente el minuto de liberación del árbitro. Los datos por partido se calculan una vez, no por
  plaza; las plazas de partidos de otra jornada se descartan.
- **Fase 5, listas de candidatos (implementada)**: `construir_candidatos(dia)` devuelve un CSR
  plano (`ListasCandidatos{offsets, candidatos}`) que `construir_instancia_dia` copia en
  `InstanciaDia::offset_candidatos` (`n_plazas + 1` fronteras) y `InstanciaDia::candidatos`. Una
  sola pasada plaza × árbitro con cuatro filtros estáticos: nivel (`nivel >= nivel_requerido`),
  ventana (`primer_inicio <= minuto_inicio <= ultimo_inicio`), vetos (ni el local ni el visitante en
  `equipos_vetados`, búsqueda binaria) y **ruta de ida y vuelta** entre domicilio y sede
  (`d(domicilio, sede) != kMinutosInalcanzable` **y** `d(sede, domicilio) != kMinutosInalcanzable`).
  La vuelta es necesaria porque el objetivo usa el retorno para el ajuste telescópico; un centinela
  ahí produciría costes falsos o negativos. No se suma el viaje a `primer_inicio`: el primer
  desplazamiento puede empezar antes de la disponibilidad, y esta es exactamente la condición de
  factibilidad que aplica `coste_asignar` en el régimen "en el domicilio". Los candidatos son ids
  **compactos** (`0..n_arbitros-1`), salen en orden ascendente dentro de cada plaza (desempate
  determinista) y la lista es condición **necesaria**, no suficiente: la factibilidad dinámica
  (llegar tarde desde donde esté en el nodo) la decide el generador de hijos. Se consultan con
  `n_candidatos(p)`, `candidatos_de(p)` (devuelve `std::span`) y `n_candidatos_totales()`. Un día
  sin árbitros deja `offsets = {0, 0}` por plaza y listas vacías.

## Conceptos pendientes de modelar

- **Tiempo de viaje**: decidido; coordenadas en `Sede` + OSRM (fase 3).
- **Franja horaria**: el estado continuo (`minuto_liberacion`) la sustituye; no se modela.
- **Plazas sin cubrir**: decidido en la función objetivo (principal prohibida, secundaria paga su
  retribución).
- **Tiempo muerto**: decidido; se cobra salvo la espera en el domicilio.
- **Representación del coste**: en céntimos enteros (`int64_t`).

## Ideas de optimización

- Agregado `Instancia` con los vectores contiguos de `Equipo`, `Sede`, `Arbitro`, `Partido` y
  `Plaza`. _Adoptado en `domain::Instancia`._
- Matriz plana (row-major) de tiempos de viaje entre sedes, precalculada una vez por día.
  _Adoptado en `InstanciaDia::distancias`._
- Vetos como bitmask (`std::vector<uint64_t>` o `std::bitset`) para factibilidad O(1). Aplazado:
  de momento son ids ordenados con búsqueda binaria al construir las listas de candidatos.
- Referencias por índice posicional en lugar de por `id`, con mapeo id -> índice en la carga.
  _Adoptado en la compactación por día._
- Arena por bloques para los estados, con punteros estables. _Adoptado en `algorithms::Arena`._
- Copiar solo `4 · N_dia` bytes del estado cuando se compacta por día. _Adoptado._
- Generar hijos con callback para que una frontera futura inserte directamente, sin vector
  intermedio; la variante con `std::vector` se mantiene por comodidad y para tests. _Adoptado._
- Structs/trivialmente copiables para permitir `memcpy` y mejorar localidad de caché. _Adoptado en
  `algorithms::Estado`._
