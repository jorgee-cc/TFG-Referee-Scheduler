// Implementación del cliente OSRM: URL, HTTP con libcurl, parseo de la matriz
// de duraciones, conversión a minutos y caché en disco.
//
// Flujo de una llamada a `matriz_minutos`:
//   1. Si hay caché y es válida, se devuelve (sin red).
//   2. Si no, se trocea el producto fuentes × destinos en bloques de
//      `maximo_por_peticion` y se pide cada bloque con `sources`/`destinations`.
//   3. Se valida que cada respuesta tenga el tamaño esperado y se rellena la
//      matriz completa.
//   4. Se fuerza la diagonal a 0 y se guarda la caché.

#include "referee_scheduler/io/osrm.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <curl/curl.h>

namespace referee_scheduler::io
{

    using algorithms::kMinutosInalcanzable;

    namespace
    {

        /// Inicializa libcurl una sola vez por proceso.
        ///
        /// `curl_global_init` no es segura entre hilos antes de libcurl 7.84;
        /// con `std::call_once` la inicialización ocurre exactamente una vez
        /// aunque varios días pidan su matriz en paralelo.
        void asegurar_curl()
        {
            static std::once_flag inicializado;
            std::call_once(inicializado,
                           []
                           {
                               if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                               {
                                   throw std::runtime_error(
                                       "OSRM: no se pudo inicializar libcurl");
                               }
                           });
        }

        /// Callback de libcurl: acumula el cuerpo de la respuesta en un string.
        std::size_t acumular_cuerpo(char *datos, std::size_t tam,
                                    std::size_t nmemb, void *usuario)
        {
            auto *cuerpo = static_cast<std::string *>(usuario);
            cuerpo->append(datos, tam * nmemb);
            return tam * nmemb;
        }

        /// Hace una petición GET y devuelve el cuerpo de la respuesta.
        ///
        /// Lanza `std::runtime_error` si libcurl falla o si el servidor no
        /// responde 200. En el mensaje de error se incluye el inicio del cuerpo,
        /// que en OSRM suele traer la causa ("NoRoute", "TooBig"...).
        std::string peticion_http(const std::string &url, long tiempo_limite)
        {
            asegurar_curl();

            CURL *curl = curl_easy_init();
            if (curl == nullptr)
            {
                throw std::runtime_error(
                    "OSRM: no se pudo crear el manejador de libcurl");
            }

            std::string cuerpo;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, acumular_cuerpo);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cuerpo);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, tiempo_limite);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "referee-scheduler-tfg/0.2");

            const CURLcode resultado = curl_easy_perform(curl);
            long estado_http = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &estado_http);
            curl_easy_cleanup(curl);

            if (resultado != CURLE_OK)
            {
                throw std::runtime_error(std::string("OSRM: fallo de red: ") +
                                         curl_easy_strerror(resultado));
            }
            if (estado_http != 200)
            {
                throw std::runtime_error(
                    "OSRM: HTTP " + std::to_string(estado_http) + ": " +
                    cuerpo.substr(0, 200));
            }

            return cuerpo;
        }

        /// FNV-1a de 64 bits: hash estable entre ejecuciones.
        ///
        /// `std::hash` no garantiza estabilidad entre versiones de la
        /// biblioteca; la caché necesita una clave que no cambie de una
        /// ejecución a otra.
        std::uint64_t fnv1a(std::string_view texto)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const unsigned char byte : texto)
            {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        /// Clave de caché: depende de la URL, el perfil y las coordenadas en
        /// orden (el orden fija los índices de la matriz).
        std::string clave_cache(const OpcionesOsrm &opciones,
                                std::span<const Coordenada> coordenadas)
        {
            std::ostringstream texto;
            texto << opciones.url << '|' << opciones.perfil << '|'
                  << coordenadas.size();
            texto << std::fixed << std::setprecision(6);
            for (const Coordenada &coordenada : coordenadas)
            {
                texto << '|' << coordenada.latitud << ','
                      << coordenada.longitud;
            }

            std::ostringstream hex;
            hex << std::hex << fnv1a(texto.str());
            return hex.str();
        }

        /// Ruta del fichero de caché para estas coordenadas.
        std::filesystem::path ruta_cache(const OpcionesOsrm &opciones,
                                         std::span<const Coordenada> coordenadas)
        {
            return std::filesystem::path(opciones.directorio_cache) /
                   ("distancias_" + clave_cache(opciones, coordenadas) + ".txt");
        }

        /// Lee la caché si existe y corresponde a `n` sedes.
        bool leer_cache(const std::filesystem::path &ruta, std::size_t n,
                        std::vector<Minutos> &destino)
        {
            std::ifstream fichero(ruta);
            if (!fichero)
            {
                return false;
            }

            std::size_t filas = 0;
            if (!(fichero >> filas) || filas != n)
            {
                return false;
            }

            std::vector<Minutos> valores(n * n);
            for (Minutos &valor : valores)
            {
                unsigned int leido = 0;
                if (!(fichero >> leido) || leido > kMinutosInalcanzable)
                {
                    return false;
                }
                valor = static_cast<Minutos>(leido);
            }

            destino = std::move(valores);
            return true;
        }

        /// Guarda la matriz en caché. Si no se puede escribir, no es un error:
        /// la caché es una optimización, no un requisito.
        void escribir_cache(const std::filesystem::path &ruta,
                            const std::vector<Minutos> &matriz, std::size_t n)
        {
            std::error_code error;
            const std::filesystem::path directorio = ruta.parent_path();
            if (!directorio.empty())
            {
                std::filesystem::create_directories(directorio, error);
            }
            if (error)
            {
                return;
            }

            std::ofstream fichero(ruta);
            if (!fichero)
            {
                return;
            }

            fichero << n << '\n';
            for (std::size_t i = 0; i < n; ++i)
            {
                for (std::size_t j = 0; j < n; ++j)
                {
                    fichero << matriz[i * n + j] << ' ';
                }
                fichero << '\n';
            }
        }

    } // namespace

    std::optional<Minutos> minutos_de_segundos(double segundos)
    {
        // Descarta negativos y NaN: un tiempo así no es una ruta válida.
        if (!(segundos >= 0.0))
        {
            return std::nullopt;
        }

        const double minutos = std::ceil(segundos / 60.0);
        if (minutos >= static_cast<double>(kMinutosInalcanzable))
        {
            // No cabe en la representación: se trata como inalcanzable.
            return kMinutosInalcanzable;
        }

        return static_cast<Minutos>(minutos);
    }

    std::optional<std::vector<Minutos>> parsear_duraciones(std::string_view json)
    {
        const std::size_t clave = json.find("\"durations\"");
        if (clave == std::string_view::npos)
        {
            return std::nullopt;
        }

        std::size_t posicion = json.find('[', clave);
        if (posicion == std::string_view::npos)
        {
            return std::nullopt;
        }

        // Se recorre el array contando corchetes: `[[...],[...]]` tiene
        // profundidad 2 dentro de la matriz, y la profundidad 0 marca el final
        // de `durations`.
        std::vector<Minutos> valores;
        int profundidad = 1;
        ++posicion;

        const char *inicio = json.data();
        const char *fin = inicio + json.size();

        while (posicion < json.size())
        {
            const char actual = json[posicion];

            if (std::isspace(static_cast<unsigned char>(actual)) != 0)
            {
                ++posicion;
                continue;
            }

            if (actual == '[')
            {
                ++profundidad;
                ++posicion;
                continue;
            }

            if (actual == ']')
            {
                --profundidad;
                ++posicion;
                if (profundidad == 0)
                {
                    return valores;
                }
                continue;
            }

            if (actual == ',')
            {
                ++posicion;
                continue;
            }

            // Sin ruta: OSRM devuelve `null` en esa celda.
            if (json.compare(posicion, 4, "null") == 0)
            {
                valores.push_back(kMinutosInalcanzable);
                posicion += 4;
                continue;
            }

            double segundos = 0.0;
            const auto conversion =
                std::from_chars(inicio + posicion, fin, segundos);
            if (conversion.ec != std::errc{})
            {
                return std::nullopt;
            }
            posicion = static_cast<std::size_t>(conversion.ptr - inicio);

            const std::optional<Minutos> minutos = minutos_de_segundos(segundos);
            if (!minutos)
            {
                return std::nullopt;
            }
            valores.push_back(*minutos);
        }

        // Se acabó el texto sin cerrar la matriz.
        return std::nullopt;
    }

    std::string construir_url(const OpcionesOsrm &opciones,
                              std::span<const Coordenada> coordenadas,
                              std::span<const std::size_t> fuentes,
                              std::span<const std::size_t> destinos)
    {
        // Sin doble barra si la URL base acaba en '/'.
        std::string base = opciones.url;
        while (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }

        std::ostringstream url;
        url << base << "/table/v1/" << opciones.perfil << '/';

        // OSRM espera `longitud,latitud` (GeoJSON), no `latitud,longitud`.
        url << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < coordenadas.size(); ++i)
        {
            if (i != 0)
            {
                url << ';';
            }
            url << coordenadas[i].longitud << ',' << coordenadas[i].latitud;
        }

        url << "?annotations=duration";

        if (!fuentes.empty())
        {
            url << "&sources=";
            for (std::size_t i = 0; i < fuentes.size(); ++i)
            {
                if (i != 0)
                {
                    url << ';';
                }
                url << fuentes[i];
            }
        }
        if (!destinos.empty())
        {
            url << "&destinations=";
            for (std::size_t i = 0; i < destinos.size(); ++i)
            {
                if (i != 0)
                {
                    url << ';';
                }
                url << destinos[i];
            }
        }

        return url.str();
    }

    OsrmDistancias::OsrmDistancias(OpcionesOsrm opciones)
        : opciones_(std::move(opciones))
    {
        // Un límite 0 haría un bucle infinito de bloques de tamaño 0.
        if (opciones_.maximo_por_peticion == 0)
        {
            opciones_.maximo_por_peticion = 1;
        }
    }

    std::vector<Minutos>
    OsrmDistancias::matriz_minutos(std::span<const Coordenada> coordenadas)
    {
        const std::size_t n = coordenadas.size();
        std::vector<Minutos> matriz(n * n, kMinutosInalcanzable);
        if (n == 0)
        {
            return matriz;
        }

        // 1. Caché: evita repetir peticiones entre ejecuciones.
        std::filesystem::path cache;
        if (!opciones_.directorio_cache.empty())
        {
            cache = ruta_cache(opciones_, coordenadas);
            if (leer_cache(cache, n, matriz))
            {
                return matriz;
            }
        }

        // 2. Peticiones por bloques de la tabla. El servidor limita las
        // dimensiones de la tabla (100 por defecto), así que se piden
        // submatrices `fuentes × destinos` de como mucho ese tamaño.
        const std::size_t bloque = opciones_.maximo_por_peticion;
        std::vector<std::size_t> fuentes;
        std::vector<std::size_t> destinos;

        for (std::size_t fila0 = 0; fila0 < n; fila0 += bloque)
        {
            const std::size_t fila1 = std::min(n, fila0 + bloque);
            for (std::size_t columna0 = 0; columna0 < n; columna0 += bloque)
            {
                const std::size_t columna1 = std::min(n, columna0 + bloque);
                const std::size_t filas = fila1 - fila0;
                const std::size_t columnas = columna1 - columna0;

                fuentes.clear();
                destinos.clear();

                // Si el bloque es toda la matriz, se omiten `sources` y
                // `destinations` para no alargar la URL sin necesidad.
                const bool matriz_completa =
                    fila0 == 0 && fila1 == n && columna0 == 0 && columna1 == n;
                if (!matriz_completa)
                {
                    for (std::size_t i = fila0; i < fila1; ++i)
                    {
                        fuentes.push_back(i);
                    }
                    for (std::size_t j = columna0; j < columna1; ++j)
                    {
                        destinos.push_back(j);
                    }
                }

                const std::string url =
                    construir_url(opciones_, coordenadas, fuentes, destinos);
                const std::string respuesta =
                    peticion_http(url, opciones_.tiempo_limite_segundos);

                const auto valores = parsear_duraciones(respuesta);
                if (!valores || valores->size() != filas * columnas)
                {
                    throw std::runtime_error(
                        "OSRM: respuesta sin una matriz de duraciones del "
                        "tamano esperado: " +
                        respuesta.substr(0, 200));
                }

                for (std::size_t i = 0; i < filas; ++i)
                {
                    for (std::size_t j = 0; j < columnas; ++j)
                    {
                        matriz[(fila0 + i) * n + (columna0 + j)] =
                            (*valores)[i * columnas + j];
                    }
                }
            }
        }

        // 3. La distancia de una sede a sí misma es 0.
        for (std::size_t i = 0; i < n; ++i)
        {
            matriz[i * n + i] = 0;
        }

        // 4. Guardar para la próxima ejecución.
        if (!cache.empty())
        {
            escribir_cache(cache, matriz, n);
        }

        return matriz;
    }

} // namespace referee_scheduler::io
