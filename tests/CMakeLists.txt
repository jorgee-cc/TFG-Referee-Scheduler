# Registro de los ejecutables de prueba del proyecto, uno por área: humo del
# estado, arena de nodos, función objetivo y construcción de los datos
# estáticos. Cada uno se enlaza con `referee_scheduler_algorithms` y se da de
# alta en CTest con `add_test`, de modo que `ctest` los ejecute todos.

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

# El cliente OSRM se prueba sin red (conversión, parseo del JSON y URL). Solo
# se registra si libcurl estaba disponible al configurar CMake.
if(TARGET referee_scheduler_io)
    add_executable(referee_scheduler_osrm_tests
        test_osrm.cpp
    )

    target_link_libraries(referee_scheduler_osrm_tests
        PRIVATE
            referee_scheduler_io
    )

    add_test(NAME referee_scheduler_osrm COMMAND referee_scheduler_osrm_tests)
endif()
