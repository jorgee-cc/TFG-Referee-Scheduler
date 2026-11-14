// Entidad de dominio `Sede`: el lugar donde se disputan partidos y, a la vez,
// el domicilio de los árbitros.
// Guarda nombre y coordenadas WGS84, que son la base para calcular los tiempos
// de viaje con un servidor de rutas en vez de almacenar una matriz N×N en la
// instancia. Header-only y sin lógica, con campos públicos como el resto de
// `domain/`.

#pragma once

#include <string>

namespace referee_scheduler::domain {

/// Representa una sede en la que se disputan partidos.
class Sede {
public:
    /// Identificador único de la sede.
    int id = 0;
    /// Nombre visible de la sede.
    std::string nombre;

    /// Latitud en grados decimales (WGS84). Positiva al norte del ecuador.
    ///
    /// Junto con `longitud` permite calcular los tiempos de viaje con un
    /// servidor de rutas (OSRM), en vez de guardar una matriz N×N en la
    /// instancia. El valor 0 es el del meridiano/ecuador: una sede sin
    /// coordenadas reales se detecta porque ambas quedan a cero.
    double latitud = 0.0;
    /// Longitud en grados decimales (WGS84). Positiva al este de Greenwich.
    double longitud = 0.0;
};

}
