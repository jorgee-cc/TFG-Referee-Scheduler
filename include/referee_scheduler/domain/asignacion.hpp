#pragma once

namespace referee_scheduler::domain {

/// Relaciona una plaza concreta con el árbitro que la cubre.
class Asignacion {
public:
    /// Identificador de la plaza cubierta.
    int plaza_id = 0;
    /// Identificador del árbitro asignado.
    int arbitro_id = 0;
};

}
