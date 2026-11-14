// Entidad de dominio `Asignacion`: el par (plaza, árbitro) que dice qué árbitro
// cubre una plaza concreta de un partido.
// Es la unidad mínima de una solución y el dato que necesita la validación
// final para reconstruir y comprobar un plan de arbitraje. Header-only, sin
// lógica y con referencias por id `int`, como el resto de `domain/`.

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
