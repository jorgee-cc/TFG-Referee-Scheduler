# Referee Scheduler

Proyecto en C++ para investigar e implementar algoritmos de busqueda aplicados a la planificacion de arbitrajes.

## Estructura

- `include/`: interfaces y tipos publicos.
- `src/`: implementaciones y punto de entrada.
- `tests/`: pruebas automatizadas.
- `examples/`: casos de uso y ejecuciones de ejemplo.
- `data/instances/`: instancias de entrada en JSON; `data/generated/` no se versiona.
- `docs/`: documentacion tecnica y decisiones de diseño.
- `scripts/`: utilidades auxiliares para ejecucion, analisis y conversion de datos.
- `cmake/`: modulos auxiliares de CMake.

## Compilar y probar

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Si no hay CMake, el script de ejecución compila directamente con `g++`.

## Ejecutar una instancia

Solo es obligatorio el fichero JSON de entrada:

```bash
python3 scripts/ejecutar.py data/instances/ejemplo.json
```

El script compila si el binario no existe o está desactualizado y ejecuta el
programa desde la raíz del repositorio (para que la caché de OSRM quede en
`data/generated/`). Por defecto guarda además el informe completo en
`data/generated/resultado_<nombre-del-input>.json`, de modo que con solo el
fichero de entrada ya se obtiene el resumen de terminal y el detalle por
árbitro. Opciones propias del script:

- `--recompilar`: fuerza la compilación.
- `--sin-compilar`: no compila; falla si no existe el binario.
- `--sin-json`: no genera el JSON por defecto.

Los demás argumentos se pasan al ejecutable. Las rutas relativas de `--salida`
se interpretan desde la raíz del repositorio, porque el script ejecuta el
binario desde allí.

```bash
python3 scripts/ejecutar.py data/instances/ejemplo.json \
    --salida data/generated/resultado.json \
    --max-nodos 500000
```

Opciones del ejecutable:

| Opción | Descripción |
|---|---|
| `--salida <ruta>` | Escribe el informe completo en JSON. |
| `--max-nodos <N>` | Presupuesto de nodos por jornada (defecto: 1.000.000). |
| `--anchura-cubo <N>` | Céntimos por cubo de la lista abierta (defecto: 1). |
| `--osrm-url <url>` | URL del servidor OSRM. |
| `--cache-dir <ruta>` | Directorio de caché de OSRM (defecto: `data/generated`). |
| `--sin-cache` | No lee ni guarda caché de OSRM. |
| `--ayuda`, `-h` | Muestra la ayuda. |

La terminal muestra un resumen con el coste, los nodos, los tiempos y los
minutos de viaje/espera/partido. **No** lista las asignaciones ni la tabla de
árbitros: con miles de plazas dejaría de ser útil. Ese detalle está en el JSON
de `--salida`.

## Formato de instancia

Un único documento JSON con las cinco colecciones del dominio. Las fechas son
ISO-8601 en **hora local** (`YYYY-MM-DDTHH:MM` o con segundos). La retribución
va en euros.

```json
{
  "equipos": [
    { "id": 1, "nombre": "Equipo A" }
  ],
  "sedes": [
    { "id": 10, "nombre": "Campo", "latitud": 43.36, "longitud": -8.41 },
    { "id": 90, "nombre": "Domicilio", "latitud": 43.37, "longitud": -8.40 }
  ],
  "arbitros": [
    {
      "id": 1,
      "nivel": 2,
      "domicilio_id": 90,
      "vetos": [1],
      "disponibilidad_inicio": "2026-11-20T08:00:00",
      "disponibilidad_fin": "2026-11-20T23:00:00"
    }
  ],
  "partidos": [
    {
      "id": 100,
      "sede_id": 10,
      "inicio": "2026-11-20T18:00:00",
      "final": "2026-11-20T19:45:00",
      "equipo_local_id": 1,
      "equipo_visitante_id": 2
    }
  ],
  "plazas": [
    {
      "id": 1000,
      "partido_id": 100,
      "rol": "principal",
      "nivel_requerido": 2,
      "retribucion": 45.0
    }
  ]
}
```

Reglas que valida el cargador:

- ids únicos y no negativos en cada colección;
- todas las referencias existen (`sede_id`, `domicilio_id`, `partido_id`,
  equipos y vetos);
- las sedes de partido y los domicilios son conjuntos disjuntos (una `Sede` es
  una ubicación, nunca las dos cosas);
- coordenadas en rango y distintas de `(0, 0)`;
- `final >= inicio` y `disponibilidad_fin >= disponibilidad_inicio`;
- cada partido tiene al menos una plaza, exactamente una `principal` y ningún
  rol repetido;
- niveles y retribuciones no negativos.

Roles admitidos: `principal`, `asistente`, `anotador` y `juez_linea` (también
`juez-linea` o `juez de linea`).

## Salida JSON

El informe de `--salida` incluye, por jornada:

- causa (`optima`, `sin_solucion`, `presupuesto_agotado`), coste total y
  desglose (`coste_tiempo_centimos`, `retribucion_perdida_centimos`);
- plazas cubiertas, árbitros usados, minutos de viaje/espera/partido y tarifa
  efectiva (`euros_por_hora`);
- métricas de búsqueda: nodos creados/expandidos/descartados, estados
  distintos, lista abierta máxima, nodos por segundo y memoria;
- todas las asignaciones (`plaza_id`, `partido_id`, `rol`, `arbitro_id` o
  `null`, retribución);
- las métricas de todos los árbitros que trabajan en esa jornada.

Al final hay un `arbitros_globales` con la suma de todas las jornadas y los
totales de la instancia.

## Instancias de ejemplo

En `data/instances/` hay varias instancias para probar el cargador y el motor:

- `ejemplo_minimo.json`: 1 partido, 1 árbitro, 1 plaza.
- `ejemplo_pequeno.json`: 2 partidos y 4 plazas.
- `ejemplo_muchos_arbitros.json`: 40 árbitros para pocas plazas.
- `ejemplo_muchas_plazas.json`: 10 partidos y 20 plazas.
- `ejemplo_muchos_arbitros_muchas_plazas.json`: 60 árbitros, 15 partidos y 34
  plazas; es un caso de estrés.
- `ejemplo_multi_dia.json`: viernes, sábado y domingo.
- `ejemplo_vetos_niveles.json`: niveles, vetos y ventanas variadas.
- `ejemplo_nocturno.json`: partido que empieza a las 23:30 y acaba de
  madrugada.
- `ejemplo_sin_solucion.json`: instancia válida pero infactible, para probar el
  camino `SinSolucion`.
- `ejemplo_coordenadas_dispersas.json`: domicilios en varias localidades.

Los ejemplos grandes pueden agotar el presupuesto de nodos (`--max-nodos`) y
terminar con `presupuesto_agotado`; es el comportamiento esperado de Dijkstra
sin heurística, no un error de carga. Para ejecuciones rápidas empieza por
`ejemplo_minimo.json` o `ejemplo_pequeno.json`.
