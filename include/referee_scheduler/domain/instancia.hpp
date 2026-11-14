// Agregado de entrada del problema: una `Instancia` reúne todos los vectores de
// entidades (equipos, sedes, árbitros, partidos y plazas) de la competición.
// Es el contenedor que llenará el cargador de ficheros y la entrada de la
// construcción de los datos estáticos por día. Header-only y sin lógica: las
// referencias entre entidades son ids `int`, nunca índices de vector.

#pragma once

#include <vector>

#include "referee_scheduler/domain/arbitro.hpp"
#include "referee_scheduler/domain/equipo.hpp"
#include "referee_scheduler/domain/partido.hpp"
#include "referee_scheduler/domain/plaza.hpp"
#include "referee_scheduler/domain/sede.hpp"

namespace referee_scheduler::domain
{

    /// Instancia completa de entrada: todas las entidades de la competición.
    ///
    /// Es solo un contenedor de datos (campos públicos, sin lógica), el punto de
    /// partida del cargador de ficheros y la entrada de la construcción
    /// de los datos estáticos. Las entidades se guardan en vectores contiguos y
    /// las referencias entre ellas siguen siendo por `id`, no por posición: la
    /// conversión `id -> índice` la hace cada fase al compactar.
    ///
    /// Un mismo `Instancia` cubre la jornada completa (viernes, sábado y domingo).
    /// La separación por días la hace `algorithms::agrupar_por_dia`, que trabaja
    /// sobre `partidos` sin modificar esta estructura.
    class Instancia
    {
    public:
        /// Equipos participantes.
        std::vector<Equipo> equipos;
        /// Sedes donde se disputan los partidos.
        std::vector<Sede> sedes;
        /// Árbitros disponibles en la competición (no necesariamente cada día).
        std::vector<Arbitro> arbitros;
        /// Partidos a cubrir.
        std::vector<Partido> partidos;
        /// Plazas (huecos por rol) de todos los partidos.
        std::vector<Plaza> plazas;
    };

} // namespace referee_scheduler::domain
