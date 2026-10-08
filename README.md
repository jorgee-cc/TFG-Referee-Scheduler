# Referee Scheduler

Proyecto en C++ para investigar e implementar algoritmos de busqueda aplicados a la planificacion de arbitrajes.

## Estructura

- `include/`: interfaces y tipos publicos.
- `src/`: implementaciones y punto de entrada.
- `tests/`: pruebas automatizadas.
- `examples/`: casos de uso y ejecuciones de ejemplo.
- `data/`: instancias de entrada y resultados; no se incluyen datos generados.
- `docs/`: documentacion tecnica y decisiones de diseño.
- `scripts/`: utilidades auxiliares para ejecucion, analisis y conversion de datos.
- `cmake/`: modulos auxiliares de CMake.

## Compilar

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
