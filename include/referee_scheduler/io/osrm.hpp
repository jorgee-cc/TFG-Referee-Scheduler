// Cliente de la API `/table` de OSRM (Open Source Routing Machine).
//
// Es la implementación real de `algorithms::ProveedorDistancias`: convierte
// coordenadas en una matriz de minutos de viaje por carretera. Vive en `io/`
// (no en `algorithms/`) porque depende de libcurl y de la red.
//
// El resultado se cachea en disco (`OpcionesOsrm::directorio_cache`) para no
// repetir peticiones entre ejecuciones. Los tests usan un proveedor falso y
// nunca tocan la red; solo `parsear_duraciones`, `minutos_de_segundos` y
// `construir_url` se prueban sin conexión.

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "referee_scheduler/algorithms/proveedor_distancias.hpp"

namespace referee_scheduler::io
{

    using algorithms::Coordenada;
    using algorithms::Minutos;

    /// Configuración del cliente OSRM.
    struct OpcionesOsrm
    {
        /// URL base del servidor. El público de demostración tiene límites y no
        /// es para producción; para el TFG lo recomendable es una instancia
        /// local en Docker y poner aquí su URL.
        std::string url = "https://router.project-osrm.org";

        /// Perfil de OSRM (`driving`, `cycling`, `foot`...). El público solo
        /// sirve `driving`.
        std::string perfil = "driving";

        /// Filas/columnas máximas por petición.
        ///
        /// El servidor limita el tamaño de la tabla (100 por defecto), no el
        /// número de coordenadas de la URL, así que se trocea el producto
        /// fuentes × destinos en bloques de este tamaño.
        std::size_t maximo_por_peticion = 100;

        /// Directorio donde guardar/leer la matriz en caché. Vacío = sin caché.
        /// Se crea si no existe. Relativo al directorio de trabajo.
        std::string directorio_cache = "data/generated";

        /// Tiempo máximo por petición HTTP, en segundos.
        long tiempo_limite_segundos = 60;
    };

    /// Convierte los segundos de OSRM a minutos enteros, redondeando hacia
    /// arriba.
    ///
    /// El redondeo hacia arriba es deliberado: conserva la desigualdad
    /// triangular al pasar a minutos enteros, que es lo que la función objetivo
    /// necesita para que ninguna transición cueste menos de épsilon.
    ///
    /// @return `std::nullopt` si el valor no es un número no negativo (OSRM usa
    ///         `null` para "sin ruta", que el llamador traduce a
    ///         `kMinutosInalcanzable`), o `kMinutosInalcanzable` si el tiempo
    ///         no cabe en `Minutos`.
    std::optional<Minutos> minutos_de_segundos(double segundos);

    /// Extrae la matriz `durations` de una respuesta JSON de OSRM.
    ///
    /// Parser mínimo: OSRM devuelve un objeto con una única matriz de números o
    /// `null`, así que no hace falta un parser JSON completo. Los `null` se
    /// convierten en `kMinutosInalcanzable`.
    ///
    /// @return Los valores en orden row-major, o `std::nullopt` si la respuesta
    ///         no contiene una matriz `durations` válida.
    std::optional<std::vector<Minutos>> parsear_duraciones(std::string_view json);

    /// Construye la URL de una petición `/table`.
    ///
    /// Las coordenadas van en orden **`longitud,latitud`** (GeoJSON), con 6
    /// decimales. Si `fuentes`/`destinos` están vacíos se piden todas; en otro
    /// caso se listan sus índices para pedir una submatriz.
    std::string construir_url(const OpcionesOsrm &opciones,
                              std::span<const Coordenada> coordenadas,
                              std::span<const std::size_t> fuentes,
                              std::span<const std::size_t> destinos);

    /// Proveedor de distancias contra un servidor OSRM.
    ///
    /// Lanza `std::runtime_error` si la red falla o la respuesta no se puede
    /// interpretar: un error de entrada/salida no es un fallo de programación
    /// que deba resolverse con `assert`.
    class OsrmDistancias final : public algorithms::ProveedorDistancias
    {
    public:
        /// Crea el cliente; no hace ninguna petición hasta `matriz_minutos`.
        explicit OsrmDistancias(OpcionesOsrm opciones = {});

        /// Matriz `n × n` de minutos para las coordenadas dadas.
        ///
        /// Usa la caché si está activada y existe; si no, trocea la petición en
        /// bloques, la pide a OSRM y guarda el resultado. La diagonal se fuerza
        /// a 0.
        std::vector<Minutos>
        matriz_minutos(std::span<const Coordenada> coordenadas) override;

    private:
        /// Configuración del cliente.
        OpcionesOsrm opciones_;
    };

} // namespace referee_scheduler::io
