// Tipo de solución heredado: agrupa las asignaciones de un plan junto a dos
// mapas auxiliares (última sede y franja ocupada por árbitro).
// Está superado por `algorithms::Estado`, la estadística suficiente empaquetada
// en 4 B por árbitro que usa la búsqueda, y su concepto de franja lo sustituyen
// los minutos continuos. No se amplía ni se usa en código nuevo: se conserva a
// la espera de decidir si se borra o queda como tipo de la solución final.

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

    /// MAPA ESPACIAL: dónde está cada árbitro en este momento.
    /// El índice es el id del árbitro; el valor, el id de su sede actual.
    std::vector<int> ultima_sede_arbitro;

    /// MAPA TEMPORAL: qué franja horaria ocupa cada árbitro.
    /// El índice es el id del árbitro; el valor, el id de la franja ocupada
    /// (-1 si el árbitro aún no tiene ninguna franja ocupada).
    std::vector<int> franja_ocupada_arbitro;
};

}
