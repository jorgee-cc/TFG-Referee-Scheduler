// Tipos de datos estáticos de UNA jornada: el árbitro ya compactado y, más
// adelante, las plazas, la matriz de distancias y las listas de candidatos.
// Indexar por `0..N_dia-1` (en vez de por el id global) es lo que permite que
// `Estado` y `Arena` no guarden ranuras de árbitros que ese día no trabajan.

#pragma once

#include <vector>

#include "referee_scheduler/algorithms/tiempo.hpp"

namespace referee_scheduler::algorithms
{

    /// Árbitro de una jornada concreta, ya compactado.
    ///
    /// `InstanciaDia` guarda estos registros en un vector y el índice dentro del
    /// vector **es** el identificador compacto (`0..N_dia-1`) que usan el estado
    /// y la arena. `id_global` conserva el id original de `domain::Arbitro` para
    /// poder reconstruir la solución al final.
    ///
    /// Solo contiene atributos estáticos (los que se pueden conocer antes de
    /// buscar): no hay historial, porque el estado empaquetado no lo guarda.
    struct ArbitroDia
    {
        /// Id del árbitro en `domain::Instancia`; el índice del vector es el id compacto.
        int id_global = 0;

        /// Nivel del árbitro. Una plaza es candidata si `nivel >= nivel_requerido`.
        int nivel = 0;

        /// Id global de su sede-domicilio. La fase 3 lo traduce al índice
        /// compacto de la matriz de distancias.
        int sede_domicilio_global = 0;

        /// Primer minuto del día en que puede empezar (0 si ya estaba disponible
        /// antes de que empezara la jornada). Se interpreta como "disponible en
        /// su domicilio desde este minuto".
        int primer_inicio = 0;

        /// Último minuto del día en que puede **empezar** un partido
        /// (`kMinutosPorDia` = 1440 si su disponibilidad cubre el día entero).
        ///
        /// No es la hora a la que debe acabar: un árbitro disponible hasta las
        /// 23:00 sí puede coger un partido que empieza a las 23:00 aunque
        /// termine después de medianoche.
        int ultimo_inicio = kMinutosPorDia;

        /// Ids globales de los equipos que no puede arbitrar, limitados a los
        /// equipos que juegan ese día y ordenados para poder usar búsqueda
        /// binaria en el filtro de candidatos.
        std::vector<int> equipos_vetados;
    };

} // namespace referee_scheduler::algorithms
