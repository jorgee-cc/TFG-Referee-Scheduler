#pragma once

#include <utility>
#include <vector>

#include "referee_scheduler/domain/asignacion.hpp"

namespace referee_scheduler::domain {

/// Contiene el conjunto de asignaciones de una solución del planificador.
class Estado {
public:
    /// Construye un estado sin asignaciones.
    Estado() = default;

    /// Construye un estado tomando las asignaciones proporcionadas.
    explicit Estado(std::vector<Asignacion> asignaciones)
        : asignaciones(std::move(asignaciones)) {}

    /// Asignaciones de árbitros a plazas que forman el estado actual.
    std::vector<Asignacion> asignaciones;
};

}
