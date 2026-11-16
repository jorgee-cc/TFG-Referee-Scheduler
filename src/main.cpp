// Punto de entrada del ejecutable `referee_scheduler`.
//
// Uso:
//   referee_scheduler <instancia.json> [opciones]
//
// Solo el fichero de entrada es obligatorio. El programa carga la instancia,
// construye los datos estáticos de cada jornada, lanza Dijkstra y escribe:
//  - un resumen legible por la terminal (coste, nodos, tiempos y minutos);
//  - un informe JSON completo si se pasa `--salida`.
//
// El resumen de texto no lista asignaciones ni árbitros para no inundar la
// terminal con instancias grandes. El detalle está en el JSON.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "referee_scheduler/algorithms/busqueda.hpp"
#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/algorithms/orquestacion.hpp"
#include "referee_scheduler/domain/instancia.hpp"
#include "referee_scheduler/io/informe.hpp"
#include "referee_scheduler/io/instancia_json.hpp"

#ifdef REFEREE_SCHEDULER_CON_CURL
#include "referee_scheduler/io/osrm.hpp"
#endif

using namespace referee_scheduler;

namespace
{

    struct OpcionesCli
    {
        std::string entrada;
        std::optional<std::string> salida;

        std::size_t max_nodos = 1'000'000;
        std::int64_t anchura_cubo = 1;

        std::string osrm_url;
        std::string cache_dir;
        bool sin_cache = false;

        bool ayuda = false;
    };

    void escribir_uso(std::ostream &salida, std::string_view programa)
    {
        salida << "Uso: " << programa
               << " <instancia.json> [opciones]\n\n"
                  "Opciones:\n"
                  "  --salida <ruta>      Escribe el informe completo en JSON\n"
                  "  --max-nodos <N>      Presupuesto de nodos por jornada "
                  "(defecto: 1000000)\n"
                  "  --anchura-cubo <N>   Céntimos por cubo de la lista abierta "
                  "(defecto: 1)\n"
                  "  --osrm-url <url>     URL del servidor OSRM\n"
                  "  --cache-dir <ruta>   Directorio de caché de OSRM "
                  "(defecto: data/generated)\n"
                  "  --sin-cache          No usa ni guarda caché de OSRM\n"
                  "  --ayuda, -h          Muestra esta ayuda\n";
    }

    bool leer_entero(std::string_view texto, long long &valor)
    {
        const char *inicio = texto.data();
        const char *fin = inicio + texto.size();
        const auto resultado = std::from_chars(inicio, fin, valor);
        return resultado.ec == std::errc() && resultado.ptr == fin;
    }

    /// Devuelve `false` si hay un error de argumentos ya explicado por `error`.
    bool parsear_argumentos(int argc, char **argv, OpcionesCli &opciones,
                            std::string &error)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argumento = argv[i];

            if (argumento == "--ayuda" || argumento == "-h")
            {
                opciones.ayuda = true;
                continue;
            }

            if (argumento == "--sin-cache")
            {
                opciones.sin_cache = true;
                continue;
            }

            auto siguiente = [&](std::string_view nombre, std::string &destino)
            {
                if (i + 1 >= argc)
                {
                    error = "falta el valor de " + std::string(nombre);
                    return false;
                }
                destino = argv[++i];
                return true;
            };

            if (argumento == "--salida")
            {
                std::string destino;
                if (!siguiente(argumento, destino))
                {
                    return false;
                }
                opciones.salida = std::move(destino);
                continue;
            }

            if (argumento == "--osrm-url")
            {
                if (!siguiente(argumento, opciones.osrm_url))
                {
                    return false;
                }
                continue;
            }

            if (argumento == "--cache-dir")
            {
                if (!siguiente(argumento, opciones.cache_dir))
                {
                    return false;
                }
                continue;
            }

            if (argumento == "--max-nodos" || argumento == "--anchura-cubo")
            {
                std::string valor;
                if (!siguiente(argumento, valor))
                {
                    return false;
                }

                long long numero = 0;
                if (!leer_entero(valor, numero))
                {
                    error = "valor no entero en " + std::string(argumento) +
                            ": '" + valor + "'";
                    return false;
                }

                if (numero <= 0)
                {
                    error = std::string(argumento) + " debe ser positivo";
                    return false;
                }

                if (argumento == "--max-nodos")
                {
                    opciones.max_nodos = static_cast<std::size_t>(numero);
                }
                else
                {
                    opciones.anchura_cubo = static_cast<std::int64_t>(numero);
                }
                continue;
            }

            if (!argumento.empty() && argumento.front() == '-')
            {
                error = "opción desconocida: " + std::string(argumento);
                return false;
            }

            if (!opciones.entrada.empty())
            {
                error = "solo se puede pasar un fichero de instancia";
                return false;
            }
            opciones.entrada = argumento;
        }

        if (opciones.ayuda)
        {
            return true;
        }

        if (opciones.entrada.empty())
        {
            error = "falta el fichero de instancia";
            return false;
        }

        return true;
    }

} // namespace

int main(int argc, char **argv)
{
    OpcionesCli opciones;
    std::string error;

    if (!parsear_argumentos(argc, argv, opciones, error))
    {
        std::cerr << "Error: " << error << "\n\n";
        escribir_uso(std::cerr, argv[0]);
        return 1;
    }

    if (opciones.ayuda)
    {
        escribir_uso(std::cout, argv[0]);
        return 0;
    }

#ifndef REFEREE_SCHEDULER_CON_CURL
    std::cerr << "Error: este binario se compiló sin libcurl, así que no tiene "
                 "proveedor de distancias OSRM.\n"
                 "Instala libcurl y vuelve a configurar el proyecto para "
                 "poder resolver instancias.\n";
    return 1;
#else
    try
    {
        const domain::Instancia instancia =
            io::cargar_instancia_json(opciones.entrada);

        io::OpcionesOsrm opciones_osrm;
        if (!opciones.osrm_url.empty())
        {
            opciones_osrm.url = opciones.osrm_url;
        }
        if (!opciones.cache_dir.empty())
        {
            opciones_osrm.directorio_cache = opciones.cache_dir;
        }
        if (opciones.sin_cache)
        {
            opciones_osrm.directorio_cache.clear();
        }

        io::OsrmDistancias proveedor(opciones_osrm);

        algorithms::OpcionesBusqueda opciones_busqueda;
        opciones_busqueda.max_nodos = opciones.max_nodos;
        opciones_busqueda.anchura_cubo = opciones.anchura_cubo;

        const algorithms::ResumenEjecucion resumen =
            algorithms::resolver_instancia(
                instancia, proveedor, algorithms::OpcionesDistancias{},
                algorithms::ParametrosObjetivo{}, opciones_busqueda);

        io::escribir_resumen_texto(std::cout, resumen, opciones.entrada);

        if (opciones.salida)
        {
            std::ofstream salida(*opciones.salida, std::ios::binary);
            if (!salida)
            {
                std::cerr << "Error: no se pudo abrir el fichero de salida: "
                          << *opciones.salida << '\n';
                return 1;
            }

            io::escribir_informe_json(salida, resumen, opciones.entrada);
            if (!salida)
            {
                std::cerr << "Error: no se pudo escribir el informe JSON en "
                          << *opciones.salida << '\n';
                return 1;
            }

            std::cout << "\nInforme JSON: " << *opciones.salida << '\n';
        }

        return resumen.hay_solucion ? 0 : 2;
    }
    catch (const std::exception &excepcion)
    {
        std::cerr << "Error: " << excepcion.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Error: fallo desconocido durante la ejecución.\n";
        return 1;
    }
#endif
}
