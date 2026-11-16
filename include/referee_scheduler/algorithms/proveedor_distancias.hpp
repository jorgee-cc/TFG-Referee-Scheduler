// Interfaz de la matriz de tiempos de viaje .
// La construcción no sabe de dónde salen los tiempos: pide una matriz cuadrada
// de minutos a un `ProveedorDistancias`. La implementación real (OSRM) vive en
// `io/`, de modo que `algorithms/` no depende de la red ni de libcurl, y los
// tests pueden inyectar una matriz fija sin tocar el disco.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace referee_scheduler::algorithms
{

    /// Minutos de viaje entre dos ubicaciones.
    ///
    /// `uint16_t` llega de sobra (un viaje de 65535 minutos son 45 días) y deja
    /// espacio para el centinela de "no hay ruta".
    using Minutos = std::uint16_t;

    /// Valor que representa "no hay ruta entre estas dos ubicaciones".
    ///
    /// Se usa el mayor valor utilizable (65534) para que una suma de dos
    /// distancias reales nunca lo alcance por accidente. El filtro de
    /// candidatos descarta las parejas con este valor.
    inline constexpr Minutos kMinutosInalcanzable = 65534;

    /// Tiempo mínimo de un desplazamiento entre dos ubicaciones distintas.
    ///
    /// OSRM devuelve 0 minutos cuando las dos coordenadas están a pocos metros
    /// (por ejemplo, un domicilio y la sede del partido en la misma calle).
    /// Físicamente hay que salir y llegar, así que ningún desplazamiento es
    /// gratis: la fase 3 eleva cualquier 0 entre ubicaciones distintas a este
    /// mínimo. La diagonal (una ubicación consigo misma) sigue siendo 0, porque
    /// ahí no hay desplazamiento.
    inline constexpr Minutos kMinutosViajeMinimo = 1;

    /// Coordenadas geográficas de una ubicación, en grados decimales (WGS84).
    struct Coordenada
    {
        /// Latitud: positiva al norte del ecuador.
        double latitud = 0.0;
        /// Longitud: positiva al este de Greenwich.
        double longitud = 0.0;
    };

    /// Fuente de tiempos de viaje entre un conjunto de ubicaciones.
    ///
    /// El contrato es deliberadamente pequeño: dado el conjunto de ubicaciones
    /// del día (en el orden que fija la compactación), devuelve la matriz
    /// completa `n × n` en minutos, en orden row-major (`m[i * n + j]`).
    ///
    /// **La matriz puede ser asimétrica**: en la carretera ir de A a B no tiene
    /// por qué costar lo mismo que ir de B a A (sentidos únicos, giros). El
    /// proveedor no debe espejarla.
    ///
    /// - La diagonal se ignora: la construcción la fuerza a 0.
    /// - Una pareja sin ruta se devuelve como `kMinutosInalcanzable`.
    class ProveedorDistancias
    {
    public:
        virtual ~ProveedorDistancias() = default;

        /// Matriz `n × n` de minutos para las `coordenadas` recibidas.
        ///
        /// @param coordenadas Ubicaciones en el orden en que se quieren indexar.
        /// @return `n * n` valores; `n = coordenadas.size()`.
        virtual std::vector<Minutos>
        matriz_minutos(std::span<const Coordenada> coordenadas) = 0;
    };

} // namespace referee_scheduler::algorithms
