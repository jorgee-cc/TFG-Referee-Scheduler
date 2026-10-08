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

add_executable(referee_scheduler
    src/main.cpp
)

target_include_directories(referee_scheduler
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
