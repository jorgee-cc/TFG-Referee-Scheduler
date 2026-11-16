# Construcción del proyecto TFG de planificación de arbitrajes.
# Define estos targets:
#  - `referee_scheduler_domain`: cabeceras del modelo (header-only);
#  - `referee_scheduler_algorithms`: búsqueda, datos estáticos y orquestación;
#  - `referee_scheduler_io`: cargador JSON, informes y, si hay libcurl, OSRM;
#  - el ejecutable `referee_scheduler`.
# C++20 estricto, sin extensiones del compilador, y tests activados por defecto.

cmake_minimum_required(VERSION 3.20)

project(referee_scheduler
    VERSION 0.1.0
    DESCRIPTION "Algoritmos de busqueda para la planificacion de arbitrajes"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build the test suite" ON)

add_library(referee_scheduler_domain INTERFACE)

target_include_directories(referee_scheduler_domain
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

# Algoritmos de búsqueda, construcción de los datos estáticos y orquestación
# por jornadas. Dependen del modelo de dominio (ids, entidades).
add_library(referee_scheduler_algorithms STATIC
    src/algorithms/busqueda.cpp
    src/algorithms/construir_instancia.cpp
    src/algorithms/instancia_dia.cpp
    src/algorithms/metricas.cpp
    src/algorithms/orquestacion.cpp
    src/algorithms/tiempo.cpp
    src/algorithms/transiciones.cpp
)

target_link_libraries(referee_scheduler_algorithms
    PUBLIC
        referee_scheduler_domain
)

# Entrada/salida: parser JSON, cargador de instancias e informes. El cliente
# OSRM solo se añade si libcurl está disponible; el resto (y el ejecutable) se
# construyen igualmente, aunque sin proveedor de distancias reales.
add_library(referee_scheduler_io STATIC
    src/io/informe.cpp
    src/io/instancia_json.cpp
    src/io/json.cpp
)

target_link_libraries(referee_scheduler_io
    PUBLIC
        referee_scheduler_algorithms
)

find_package(CURL QUIET)

if(CURL_FOUND)
    target_sources(referee_scheduler_io
        PRIVATE
            src/io/osrm.cpp
    )

    target_link_libraries(referee_scheduler_io
        PRIVATE
            CURL::libcurl
    )

    # `main.cpp` incluye el cliente OSRM solo si esta macro está definida.
    target_compile_definitions(referee_scheduler_io
        PUBLIC
            REFEREE_SCHEDULER_CON_CURL=1
    )

    # Prueba manual contra la red; no se registra en CTest.
    add_executable(referee_scheduler_osrm_smoke
        examples/osrm_smoke.cpp
    )

    target_link_libraries(referee_scheduler_osrm_smoke
        PRIVATE
            referee_scheduler_io
    )
else()
    message(STATUS "libcurl no encontrado: el ejecutable se compila sin OSRM")
endif()

add_executable(referee_scheduler
    src/main.cpp
)

target_link_libraries(referee_scheduler
    PRIVATE
        referee_scheduler_io
)

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
