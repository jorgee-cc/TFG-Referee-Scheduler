# Optimizaciones

Registro de optimizaciones de hardware y C++20 estudiadas para el TFG. Cada sección documenta la
idea, su verificación técnica frente al código real y el plan de aplicación.

> **Estado**: ninguna de estas optimizaciones está implementada. Se explorarán **cuando el
> algoritmo esté funcionando** (paso 6 del roadmap, ensamblado de Dijkstra/A\*), porque sin una
> carga real que medir no se puede afirmar qué mejora aporta cada una. La prioridad es tener el
> planificador correcto primero; la arena ya implementada (`algorithms::Arena`, bloques de 64 KiB,
> división entera y `std::make_unique`) es funcional y suficiente para esa primera versión.

## Arena

Referencias: [`Arena`](../include/referee_scheduler/algorithms/arena.hpp) (implementación actual),
[guía de arenas de protobuf](https://protobuf.dev/reference/cpp/arenas/) y
[shantanu/arena-allocator](https://github.com/shantanu/arena-allocator).

### Punto de partida (arena v1)

Cada nodo es un bloque de `24 + 4·n_arbitros` bytes (cabecera de decisión + un `EstadoArbitro` por
árbitro). Los nodos se agrupan en bloques de heap de 64 KiB, asignados con
`std::make_unique<std::byte[]>`, y se localizan con `nodo / nodos_por_bloque_` y
`nodo % nodos_por_bloque_`. Garantiza direcciones estables y copia solo las `n_arbitros` entradas
significativas.

### Tres áreas de micro-optimización

#### 1. Coste de inicialización (zeroing) — confirmado

`std::make_unique<std::byte[]>(n)` ejecuta `new std::byte[n]()`, es decir *value-initialization*:
el compilador emite un `memset` de los 64 KiB de cada bloque. Ese recorrido es inútil porque
`crear()` sobrescribe inmediatamente cabecera y estado con placement new y `std::memcpy`.

**Solución**: `std::make_unique_for_overwrite<std::byte[]>(n)` (C++20, libstdc++ ≥ 11), que usa
`new std::byte[n]` sin inicializar.

**Matiz**: si en el futuro se asigna con `mmap`, el kernel entrega igualmente páginas a cero en el
primer fallo de página (no es evitable por seguridad del sistema). El ahorro de `for_overwrite` se
nota sobre todo cuando `malloc` recicla memoria ya mapeada.

#### 2. División entera implícita en `direccion()` — confirmado, con matiz

`direccion(nodo)` hace una división de 64 bits por acceso. En x86-64 cuesta ~20–40 ciclos (Zen 4
~20, Intel ~36) frente a 1 ciclo de `shr`/`and`. Pero crear un nodo ya copia 2424 B (≈38 líneas de
caché, ~100–200 ciclos) con `n=600`, así que la división pesa un 15–25 %, no lo domina todo.

**Soluciones** (más simples y rápidas que libdivide, y sin dependencias externas):

- **Cursor de bump para `crear()`**: guardar `std::byte* cursor_` y los nodos restantes de la
  región. Crear un nodo pasa a ser `ptr = cursor_; cursor_ += stride; --restantes;` → **cero
  divisiones en el camino caliente**, sin exigir potencias de 2.
- **Nodos por región potencia de 2** para el acceso por índice: `region = nodo >> bits;` y
  `dentro = nodo & mascara;` → 2 instrucciones.

**Sobre libdivide**: resuelve la división por un divisor invariante en tiempo de ejecución con
~2 multiplicaciones y shifts (~5–8 ciclos). Solo valdría la pena si se exigieran regiones de
exactamente 2 MiB con capacidad no potencia de 2. Con shift/mask no hace falta: es más rápido y no
añade una dependencia.

#### 3. TLB y hugepages — el objetivo es correcto, el mecanismo no del todo

Aumentar el tamaño de bloque **no reduce por sí solo las entradas de TLB por byte**: con páginas de
4 KiB, 2.4 GB de arena tocan ~620 000 páginas tanto en bloques de 64 KiB como de 2 MiB (cada bloque
sigue conteniendo 16 o 512 páginas de 4 KiB). Lo que reduce la presión es que esas páginas sean
**hugepages de 2 MiB**: entonces 2.4 GB son ~1200 entradas, que sí caben en la L2 TLB (~1536–2048
en Intel, ~3072 en Zen). Para conseguirlo hacen falta **las tres cosas**:

1. regiones de tamaño ≥ 2 MiB,
2. **alineadas a 2 MiB** y de tamaño **múltiplo de 2 MiB**,
3. `madvise(MADV_HUGEPAGE)`, porque Ubuntu y la mayoría de distros usan THP en modo `madvise`
   (no `always`).

Un bloque de 2 MiB mal alineado no garantiza nada. Además, el acceso a un nodo ya es un *cache
miss* de ~200 ciclos; el page-walk de una TLB miss (~20–30 ciclos si acierta el page-walk cache) es
un porcentaje, no el doble. **Hay que medirlo con el algoritmo funcionando.**

### Qué aportan las fuentes

**protobuf arenas** confirma el patrón (bump pointer, liberación en bloque, `Reset()` para
reutilizar, `SpaceUsed()`, `ArenaOptions` con tamaños de bloque y allocador personalizado) y aporta
dos ideas que la v1 no tiene:

- **Arena por unidad de trabajo** (aquí: por día/búsqueda, sin locks).
- **Reutilización** de la arena entre ejecuciones.

Sus tamaños por defecto (256 B–8 KiB) están pensados para mensajes pequeños y serían catastróficos
con nodos de 2.4 KB; se copia el *concepto* de opciones, no los valores. La lista de destructores no
aplica: todo es POD.

**arena-allocator** confirma el bump + `max_align_t` y documenta el punto clave del apartado 3: el
salto de latencia al cruzar el umbral de `mmap` (~128 KiB) y los fallos de página. La lección
operativa es **pocas asignaciones grandes y amortizadas**, no muchas de 64 KiB; también justifica
garantizar la alineación explícita y no prometer liberación individual.

### Diseño propuesto: arena v2

Unidad de asignación = **región**; unidad de nodo = *stride* fijo.

```
Región (32 MiB objetivo, 2 MiB-alineada, madvise HUGEPAGE)
┌───────────────────────────────────────────────────────────┐
│ nodo 0 │ nodo 1 │ ... │ nodo 2^k-1 │  relleno hasta 2 MiB │
└───────────────────────────────────────────────────────────┘
nodo = [ CabeceraNodo 24 B ][ EstadoArbitro × n ]  → stride = round_up(24 + 4n, 64)
```

- `nodos_por_region = 2^floor(log2(objetivo / stride))`; la región se asigna con
  `std::aligned_alloc(2 MiB, round_up(2^k·stride, 2 MiB))` + `madvise(MADV_HUGEPAGE)` (Linux;
  fallback `::operator new` con alineación 64 en otras plataformas). El relleno es ≤ 2 MiB por
  región (~0–6 % en la práctica).
- `crear()` con **cursor** (sin división); `direccion(nodo)` con **shift/mask** (sin división).
- `make_unique_for_overwrite` → `::operator new`/`aligned_alloc` crudo.
- `reiniciar()` para reutilizar regiones entre ejecuciones (patrón `Reset()` de protobuf).
- `OpcionesArena { std::size_t bytes_region = 32 MiB; bool hugepages = true; }` (patrón
  `ArenaOptions`).
- `memoria_reservada()` y `memoria_usada()` separadas.

Números con el *stride* redondeado a 64 B:

| n | stride | nodos/región (2^k) | bytes usados | región real | TLB para 2.4 GB (THP) |
|---|--------|--------------------|--------------|-------------|------------------------|
| 6 | 64 B | 524 288 | 32 MiB | 32 MiB | ~1200 entradas |
| 60 | 320 B | 65 536 | 20 MiB | 20 MiB | ~1200 |
| 120 | 512 B | 65 536 | 32 MiB | 32 MiB | ~1200 |
| 300 | 1 280 B | 16 384 | 20 MiB | 20 MiB | ~1200 |
| 600 | 2 432 B | 8 192 | 19 MiB | 20 MiB | ~1200 |

Frente a hoy (64 KiB, división, `make_unique`): 1 M de nodos con `n = 600` pasa de ~1953 bloques y
~620 000 páginas de 4 KiB a **~122 regiones y ~1200 páginas de 2 MiB**.

### Plan de aplicación (cuando el algoritmo funcione)

1. **Cambios portables primero** (sin tocar semántica): `make_unique_for_overwrite` + cursor +
   regiones potencia de 2 con shift/mask + stride a 64 B. Tests: misma cobertura, más casos de
   frontera de región y `reiniciar()`.
2. **Hugepages detrás de opción** (Linux, `MADV_HUGEPAGE`, `aligned_alloc` 2 MiB): activable y
   desactivable; test con `hugepages` on/off para garantizar comportamiento idéntico.
3. **Benchmark** en `examples/bench_arena.cpp`: N nodos con `n = 600`, ns/nodo de creación,
   ns/acceso aleatorio y RSS/fallos de página (`getrusage`), comparando v1 vs v2 y `hugepages`
   on/off. Sin medir, los porcentajes anteriores son hipótesis.
4. Actualizar `AGENTS.md` / `Apuntes.md` (fórmula del bloque, 1200 B → 2400 B, 19 → 38 líneas de
   caché) cuando se pida el mensaje de commit.

### Decisiones pendientes

1. **Tamaño de región por defecto**: 32 MiB (recomendado, ~1200 entradas TLB para 2.4 GB), 16 MiB
   o 64 MiB.
2. **Hugepages**: ¿`true` por defecto en Linux con fallback silencioso, o `false` por defecto y se
   activan en el benchmark?
3. **Alineación de nodo a 64 B**: coste ≤ 63 B/nodo (~2.5 %) y ahorra una línea por nodo.
4. **Benchmark en `examples/`**: AGENTS.md no decidió benchmark; conviene uno solo para la arena.
5. **Profundidad**: ¿v2 completa (regiones + hugepages) o primero solo los cambios portables,
   medir, y decidir hugepages con datos?

### Ideas descartadas por ahora

- **libdivide**: innecesario con regiones potencia de 2 y shift/mask.
- **NUMA** (`numa_alloc_onnode`, `mbind`): fuera del alcance del TFG.
- **Hugepages explícitas** (`MAP_HUGETLB`, requiere `nr_hugepages` y root): se prefiere THP con
  `madvise`.
- **Prefetch manual** (`__builtin_prefetch`): probar solo si el benchmark muestra que la creación
  de nodos va dominada por *cache misses*.
- **Layout SoA** (todas las cabeceras en una región y todos los estados en otra): complica el acceso
  sin una ganancia clara frente al nodo contiguo.
