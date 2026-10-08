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
};

}
