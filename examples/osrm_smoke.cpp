// Prueba manual del cliente OSRM: pide la matriz de tiempos entre unas sedes
// reales y la imprime. Hace peticiones de red, así que NO forma parte de CTest.
//
// Uso (desde la raíz del repositorio):
//   ./build/referee_scheduler_osrm_smoke
// o compilado a mano (una sola línea):
//   g++ -std=c++20 -Iinclude $(pkg-config --cflags libcurl) src/io/osrm.cpp
//       src/algorithms/*.cpp examples/osrm_smoke.cpp -o /tmp/osrm_smoke
//       $(pkg-config --libs libcurl)

#include <cstddef>
#include <exception>
#include <iostream>
#include <vector>

#include "referee_scheduler/io/osrm.hpp"

int main()
{
    using referee_scheduler::algorithms::Coordenada;
    using referee_scheduler::io::OpcionesOsrm;
    using referee_scheduler::io::OsrmDistancias;

    // Cuatro puntos de Madrid (latitud, longitud).
    const std::vector<Coordenada> sedes = {
        {40.4168, -3.7038}, // Puerta del Sol
        {40.4329, -3.6883}, // Paseo de la Castellana
        {40.4300, -3.7120}, // Argüelles
        {40.3900, -3.7000}, // Usera
    };

    OpcionesOsrm opciones;
    opciones.directorio_cache = "data/generated";

    try
    {
        OsrmDistancias osrm(opciones);
        const std::vector<referee_scheduler::io::Minutos> matriz =
            osrm.matriz_minutos(sedes);

        const std::size_t n = sedes.size();
        std::cout << "Matriz " << n << "x" << n << " en minutos:\n";
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = 0; j < n; ++j)
            {
                std::cout << matriz[i * n + j] << '\t';
            }
            std::cout << '\n';
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
