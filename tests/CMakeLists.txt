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
