#pragma once

#include <vector>

namespace referee_scheduler::domain {

/// Representa a un árbitro que puede ser asignado a un partido.
class Arbitro {
public:
    /// Identificador único del árbitro.
    int id = 0;
    /// Nivel o categoría del árbitro.
    int nivel = 0;
    /// Identificador de la sede habitual del árbitro.
    int sede_id = 0;
    /// Identificadores de los equipos que el árbitro no puede arbitrar.
    std::vector<int> vetos;
};

}
