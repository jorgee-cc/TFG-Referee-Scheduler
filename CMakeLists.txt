# Construcción del proyecto TFG de planificación de arbitrajes.
# Define tres targets: `referee_scheduler_domain` (cabeceras del modelo),
# `referee_scheduler_algorithms` (búsqueda y datos estáticos, ya con .cpp) y el
# ejecutable `referee_scheduler`. C++20 estricto, sin extensiones del
# compilador, y tests activados por defecto con CTest.

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

# Algoritmos de búsqueda y construcción de los datos estáticos. Dependen del
# modelo de dominio (ids, entidades) y ya tienen implementación en .cpp, así que
# el target deja de ser INTERFACE. Las cabeceras siguen viviendo en include/ y
# se propagan a quien enlace el target.
add_library(referee_scheduler_algorithms STATIC
    src/algorithms/construir_instancia.cpp
    src/algorithms/instancia_dia.cpp
    src/algorithms/tiempo.cpp
)

target_link_libraries(referee_scheduler_algorithms
    PUBLIC
        referee_scheduler_domain
)

add_executable(referee_scheduler
    src/main.cpp
)

target_link_libraries(referee_scheduler
    PRIVATE
        referee_scheduler_domain
)

# Cliente OSRM: necesita libcurl. Si no está, el resto del proyecto se sigue
# pudiendo compilar (la construcción en memoria usa un proveedor inyectado).
find_package(CURL QUIET)

if(CURL_FOUND)
    add_library(referee_scheduler_io STATIC
        src/io/osrm.cpp
    )

    target_link_libraries(referee_scheduler_io
        PUBLIC
            referee_scheduler_algorithms
        PRIVATE
            CURL::libcurl
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
    message(STATUS "libcurl no encontrado: se omite referee_scheduler_io (cliente OSRM)")
endif()

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
