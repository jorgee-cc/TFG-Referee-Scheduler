// Entidad de dominio `Equipo`: un club participante, con id y nombre visible.
// Existe para que los partidos y los vetos de los árbitros puedan referirse a
// él por id en vez de repetir el nombre por todas partes. Es header-only y sin
// lógica, con campos públicos como el resto de `domain/`.

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
