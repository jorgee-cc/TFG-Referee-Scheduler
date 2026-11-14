// Pruebas sin red del cliente OSRM: conversión de segundos a minutos, parseo
// del JSON de `durations` y construcción de la URL. El cliente completo se
// prueba a mano con `examples/osrm_smoke.cpp`, que sí hace peticiones.

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "referee_scheduler/algorithms/proveedor_distancias.hpp"
#include "referee_scheduler/io/osrm.hpp"

namespace
{

    using referee_scheduler::algorithms::Coordenada;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::kMinutosInalcanzable;
    using referee_scheduler::io::OpcionesOsrm;
    using referee_scheduler::io::construir_url;
    using referee_scheduler::io::minutos_de_segundos;
    using referee_scheduler::io::parsear_duraciones;

    int fallos = 0;

    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

} // namespace

int main()
{
    // ---------------------------------------------------------------------
    // Segundos -> minutos (siempre hacia arriba)
    // ---------------------------------------------------------------------
    comprobar(minutos_de_segundos(0.0) == 0, "0 s son 0 min");
    comprobar(minutos_de_segundos(0.1) == 1, "cualquier fraccion cuenta 1 min");
    comprobar(minutos_de_segundos(59.9) == 1, "59.9 s siguen siendo 1 min");
    comprobar(minutos_de_segundos(60.0) == 1, "60 s son 1 min exacto");
    comprobar(minutos_de_segundos(60.1) == 2, "60.1 s redondean a 2 min");
    comprobar(minutos_de_segundos(374.3) == 7, "374.3 s redondean a 7 min");
    comprobar(minutos_de_segundos(338.5) == 6, "338.5 s redondean a 6 min");

    comprobar(!minutos_de_segundos(-1.0).has_value(),
              "un tiempo negativo no es una ruta valida");
    comprobar(!minutos_de_segundos(std::nan("")).has_value(),
              "NaN no es una ruta valida");
    comprobar(minutos_de_segundos(1.0e9) == kMinutosInalcanzable,
              "un tiempo que no cabe se marca inalcanzable");

    // ---------------------------------------------------------------------
    // Parseo de `durations`
    // ---------------------------------------------------------------------

    // Respuesta real (recortada) del servidor de demostracion. Es asimetrica:
    // 374.3 s en un sentido y 426.5 s en el otro.
    const std::string json =
        R"({"code":"Ok","durations":[[0,374.3,338.5],[426.5,0,384.8],[444.1,293,0]],"destinations":[]})";
    const auto matriz = parsear_duraciones(json);
    comprobar(matriz.has_value(), "una respuesta valida debe parsearse");
    if (matriz)
    {
        comprobar(*matriz == std::vector<Minutos>{0, 7, 6, 8, 0, 7, 8, 5, 0},
                  "los segundos deben convertirse a minutos y conservar el orden");
    }

    // Celdas sin ruta: OSRM devuelve null.
    const auto con_nulos =
        parsear_duraciones(R"({"durations":[[0,null],[null,0]]})");
    comprobar(con_nulos.has_value(), "los null deben parsearse");
    if (con_nulos)
    {
        comprobar(*con_nulos ==
                      std::vector<Minutos>{0, kMinutosInalcanzable,
                                           kMinutosInalcanzable, 0},
                  "null debe convertirse en inalcanzable");
    }

    // Matriz vacia (sin sedes).
    const auto vacia = parsear_duraciones(R"({"durations":[]})");
    comprobar(vacia.has_value() && vacia->empty(),
              "una matriz vacia debe devolver un vector vacio");

    // Respuestas invalidas.
    comprobar(!parsear_duraciones(R"({"code":"NoRoute"})").has_value(),
              "sin durations no hay matriz");
    comprobar(!parsear_duraciones(R"({"durations":[[0,10],[10)").has_value(),
              "un JSON truncado no debe aceptarse");
    comprobar(!parsear_duraciones(R"({"durations":[[0,"x"]]})").has_value(),
              "un valor no numerico no debe aceptarse");

    // ---------------------------------------------------------------------
    // Construccion de la URL
    // ---------------------------------------------------------------------
    OpcionesOsrm opciones;
    opciones.url = "https://router.project-osrm.org/"; // barra final a proposito
    opciones.perfil = "driving";

    const std::vector<Coordenada> coordenadas = {
        {40.4168, -3.7038},
        {40.4329, -3.6883}};

    const std::string completa =
        construir_url(opciones, coordenadas, {}, {});

    comprobar(completa.rfind(
                  "https://router.project-osrm.org/table/v1/driving/", 0) == 0,
              "la URL debe empezar por la base y el perfil, sin doble barra");
    comprobar(completa.find("-3.703800,40.416800") != std::string::npos,
              "OSRM espera longitud,latitud con 6 decimales");
    comprobar(completa.find("annotations=duration") != std::string::npos,
              "la URL debe pedir las duraciones");
    comprobar(completa.find("sources=") == std::string::npos,
              "sin fuentes no se anade sources");
    comprobar(completa.find("destinations=") == std::string::npos,
              "sin destinos no se anade destinations");

    const std::vector<std::size_t> fuentes = {0};
    const std::vector<std::size_t> destinos = {1};
    const std::string submatriz =
        construir_url(opciones, coordenadas, fuentes, destinos);

    comprobar(submatriz.find("sources=0") != std::string::npos,
              "las fuentes se listan por indice");
    comprobar(submatriz.find("destinations=1") != std::string::npos,
              "los destinos se listan por indice");

    if (fallos == 0)
    {
        std::cout << "test_osrm: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
