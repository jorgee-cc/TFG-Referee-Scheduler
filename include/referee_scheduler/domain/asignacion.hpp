#pragma once

namespace referee_scheduler::domain {

/// Relaciona un partido con el árbitro que lo dirigirá.
class Asignacion {
public:
    /// Identificador del partido asignado.
    int partido_id = 0;
    /// Identificador del árbitro asignado.
    int arbitro_id = 0;
};

}
