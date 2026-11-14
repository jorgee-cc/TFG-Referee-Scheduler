// Construcción de los datos estáticos por día: este archivo declara la fase 1,
// que parte la `Instancia` completa en grupos por fecha civil (`GrupoDia`).
// La búsqueda es por jornada y en paralelo entre días, así que cada grupo es la
// entrada de las fases siguientes (compactación de árbitros y sedes, matriz de
// distancias, plazas y listas de candidatos).
// La implementación está en `src/algorithms/construir_instancia.cpp`.

#pragma once

#include <cstddef>
#include <vector>

#include "referee_scheduler/algorithms/instancia_dia.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace referee_scheduler::algorithms
{

    /// Partidos de una misma fecha civil: primer producto de los datos estáticos.
    ///
    /// La instancia de entrada cubre varias jornadas (viernes, sábado y domingo,
    /// posiblemente más). La búsqueda es **por día**: cada fecha genera una
    /// instancia independiente que se puede resolver en paralelo. Este tipo es
    /// el resultado de partir la entrada por fecha; las fases siguientes
    /// (compactación de árbitros y sedes, matriz de distancias, plazas y
    /// candidatos) consumen un `GrupoDia` cada una.
    struct GrupoDia
    {
        /// Fecha civil local a la que pertenecen todos los partidos del grupo.
        DiaCivil dia;

        /// Posiciones en `Instancia::partidos` de los partidos que empiezan ese
        /// día, en el orden de entrada.
        ///
        /// Se guardan índices y no copias para no duplicar los datos de entrada.
        /// El partido pertenece al día en que **empieza**: un encuentro del
        /// viernes a las 23:30 que termina el sábado a la 01:00 sigue siendo del
        /// viernes, aunque su `final` caiga en la fecha siguiente.
        std::vector<std::size_t> partidos;
    };

    /// Fase 1: parte la entrada por fecha civil.
    ///
    /// Devuelve un grupo por cada fecha distinta que tenga al menos un partido,
    /// ordenados del día más antiguo al más reciente. Dentro de cada grupo se
    /// respeta el orden de entrada de los partidos (que además es el orden de
    /// recorrido de `plazas`, porque una plaza referencia a su partido por id).
    ///
    /// `Partido::inicio` se descompone en hora local con las utilidades de
    /// `tiempo.hpp`, nunca restando `time_t`, para que un cambio de huso o de
    /// horario de verano no mezcle dos jornadas.
    std::vector<GrupoDia> agrupar_por_dia(const domain::Instancia &instancia);

    /// Fase 2: compacta los árbitros de un día a `0..N_dia-1`.
    ///
    /// Un árbitro entra en la jornada si su ventana de disponibilidad contiene
    /// algún minuto de **inicio** válido del día (de 00:00 a 23:59). Es decir, se
    /// compara el intervalo absoluto `[disponibilidad_inicio,
    /// disponibilidad_fin]` con el día completo, no se exige que la contenga:
    /// quien solo puede arbitrar un partido nocturno también debe entrar.
    ///
    /// La ventana se interpreta sobre la hora de inicio: estar disponible hasta
    /// las 23:00 significa poder empezar un partido a las 23:00, aunque acabe
    /// después de medianoche.
    ///
    /// El resultado sale ordenado por `id_global` ascendente, de modo que la
    /// asignación de ids compactos es determinista y no depende del orden en que
    /// el cargador haya metido los árbitros. El índice en el vector devuelto es
    /// el identificador compacto del árbitro.
    ///
    /// Los vetos se recortan a los equipos que juegan ese día (el resto no puede
    /// aparecer en ninguna plaza) y se ordenan para el filtro de candidatos.
    std::vector<ArbitroDia> compactar_arbitros(const domain::Instancia &instancia,
                                               const GrupoDia &grupo);

} // namespace referee_scheduler::algorithms
