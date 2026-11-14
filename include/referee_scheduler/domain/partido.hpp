// Entidad de dominio `Partido`: un encuentro con su sede, su horario y sus dos
// equipos. No guarda las plazas ni las retribuciones: cada hueco a cubrir es
// una `Plaza` que lo referencia por id, y ahí viven el rol, el nivel exigido y
// el dinero. Header-only y sin lógica; el horario son dos `time_t` que después
// se pasan a minutos del día.

#pragma once

#include <ctime>

namespace referee_scheduler::domain
{

    /// Representa un partido que requiere cubrir una o varias plazas.
    class Partido
    {
    public:
        /// Identificador único del partido.
        int id = 0;
        /// Identificador de la sede donde se disputa el partido.
        int sede_id = 0;
        /// Fecha y hora de inicio del partido.
        std::time_t inicio = 0;
        /// Fecha y hora de finalización del partido.
        std::time_t final = 0;
        /// Identificador del equipo local.
        int equipo_local_id = 0;
        /// Identificador del equipo visitante.
        int equipo_visitante_id = 0;
    };

}
