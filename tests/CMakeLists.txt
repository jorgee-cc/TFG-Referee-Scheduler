# Registro de los ejecutables de prueba del proyecto, uno por área: humo del
# estado, arena de nodos, función objetivo, construcción de los datos estáticos
# y transiciones de la búsqueda. Cada uno se enlaza con
# `referee_scheduler_algorithms` y se da de alta en CTest con `add_test`, de modo
# que `ctest` los ejecute todos.

add_executable(referee_scheduler_tests
    test_smoke.cpp
)

target_link_libraries(referee_scheduler_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_smoke COMMAND referee_scheduler_tests)

# La arena tiene sus propias pruebas: layout de los nodos, copia de solo
# n_arbitros entradas y estabilidad de direcciones al crecer.
add_executable(referee_scheduler_arena_tests
    test_arena.cpp
)

target_link_libraries(referee_scheduler_arena_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_arena COMMAND referee_scheduler_arena_tests)

# La función objetivo tiene sus propias pruebas: épsilon, espera gratis en
# casa, ajuste de retorno, llegada tarde y plazas vacías.
add_executable(referee_scheduler_objetivo_tests
    test_objetivo.cpp
)

target_link_libraries(referee_scheduler_objetivo_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_objetivo COMMAND referee_scheduler_objetivo_tests)

# La construcción de los datos estáticos tiene sus propias pruebas, una por
# fase: partición por día, compactación, matriz de distancias, orden de plazas
# y listas de candidatos.
add_executable(referee_scheduler_instancia_tests
    test_instancia.cpp
)

target_link_libraries(referee_scheduler_instancia_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_instancia COMMAND referee_scheduler_instancia_tests)

# Las transiciones del paso 3: estado inicial, raíz, generación de hijos (con
# vector y con callback) y reconstrucción de la solución.
add_executable(referee_scheduler_transiciones_tests
    test_transiciones.cpp
)

target_link_libraries(referee_scheduler_transiciones_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_transiciones COMMAND referee_scheduler_transiciones_tests)

# La lista abierta (cubos por céntimo): orden de extracción, empates LIFO, base
# que solo avanza y prueba de propiedad contra una priority_queue.
add_executable(referee_scheduler_abierta_tests
    test_abierta.cpp
)

target_link_libraries(referee_scheduler_abierta_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_abierta COMMAND referee_scheduler_abierta_tests)

# La tabla de estados (lista cerrada): alta, comparación exacta por nivel y
# estado, reemplazo solo con coste menor y marcado del nodo superado.
add_executable(referee_scheduler_tabla_estados_tests
    test_tabla_estados.cpp
)

target_link_libraries(referee_scheduler_tabla_estados_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_tabla_estados COMMAND referee_scheduler_tabla_estados_tests)

# El motor de Dijkstra de punta a punta: costes exactos, causa de parada,
# presupuesto y validación contra un recorrido exhaustivo.
add_executable(referee_scheduler_busqueda_tests
    test_busqueda.cpp
)

target_link_libraries(referee_scheduler_busqueda_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_busqueda COMMAND referee_scheduler_busqueda_tests)

# El parser JSON y el cargador de instancias: sintaxis, esquema, referencias y
# errores contextualizados.
add_executable(referee_scheduler_json_tests
    test_json.cpp
)

target_link_libraries(referee_scheduler_json_tests
    PRIVATE
        referee_scheduler_io
)

add_test(NAME referee_scheduler_json COMMAND referee_scheduler_json_tests)

# Informes: resumen de terminal sin volcado y JSON completo reparseable.
add_executable(referee_scheduler_informe_tests
    test_informe.cpp
)

target_link_libraries(referee_scheduler_informe_tests
    PRIVATE
        referee_scheduler_io
)

add_test(NAME referee_scheduler_informe COMMAND referee_scheduler_informe_tests)

# Métricas por árbitro reconstruidas con una matriz fija: viajes, esperas,
# retribuciones y tarifa efectiva.
add_executable(referee_scheduler_metricas_tests
    test_metricas.cpp
)

target_link_libraries(referee_scheduler_metricas_tests
    PRIVATE
        referee_scheduler_algorithms
)

add_test(NAME referee_scheduler_metricas COMMAND referee_scheduler_metricas_tests)

# Orquestación de una instancia completa con un proveedor de distancias falso:
# días triviales, sin solución y búsqueda real.
add_executable(referee_scheduler_orquestacion_tests
    test_orquestacion.cpp
)

target_link_libraries(referee_scheduler_orquestacion_tests
    PRIVATE
        referee_scheduler_io
)

add_test(NAME referee_scheduler_orquestacion COMMAND referee_scheduler_orquestacion_tests)

# El cliente OSRM se prueba sin red (conversión, parseo del JSON y URL). Solo
# se registra si libcurl estaba disponible al configurar CMake.
if(TARGET CURL::libcurl)
    add_executable(referee_scheduler_osrm_tests
        test_osrm.cpp
    )

    target_link_libraries(referee_scheduler_osrm_tests
        PRIVATE
            referee_scheduler_io
    )

    add_test(NAME referee_scheduler_osrm COMMAND referee_scheduler_osrm_tests)
endif()
