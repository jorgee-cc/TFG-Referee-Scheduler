#pragma once

#include <string>

namespace referee_scheduler::domain {

/// Representa a un equipo que participa en los partidos.
class Equipo {
public:
    /// Identificador único del equipo.
    int id = 0;
    /// Nombre visible del equipo.
    std::string nombre;
};

}
