// Métricas de una jornada reconstruida.
//
// El buscador devuelve la solución como una lista de decisiones
// (`plaza_id -> arbitro_global`) y el coste. Para el informe hace falta algo
// más: cuánto gana cada árbitro, cuántos minutos viaja, cuánto espera y cuánto
// tiempo dedica en total. Esos datos no están en el estado empaquetado (que solo
// guarda sede y minuto de liberación), así que se reconstruyen recorriendo la
// solución en orden cronológico con la matriz de distancias del día.
//
// Este módulo no depende de OSRM ni del cargador: recibe una `InstanciaDia` ya
// construida y una `SolucionDia`, y se puede probar con matrices fijas.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "referee_scheduler/algorithms/instancia_dia.hpp"
#include "referee_scheduler/algorithms/transiciones.hpp"

namespace referee_scheduler::algorithms
{

    /// Tiempo y dinero de un árbitro en una jornada.
    ///
    /// Solo se generan entradas para árbitros con al menos una plaza asignada;
    /// los que no trabajan no aparecen (su contribución es cero).
    struct MetricasArbitro
    {
        /// Id global del árbitro (`domain::Arbitro::id`).
        int arbitro_global = 0;

        /// Nivel del árbitro en esta jornada.
        int nivel = 0;

        /// Plazas que cubre.
        std::size_t n_plazas = 0;

        /// Suma de las retribuciones de las plazas cubiertas, en céntimos.
        std::int64_t retribucion_centimos = 0;

        /// Trayectos contados: salida de casa, traslados entre sedes y vuelta.
        /// Un árbitro con una sola plaza tiene 2 (ida y vuelta).
        std::size_t n_desplazamientos = 0;

        /// Minutos de conducción, incluida la vuelta a casa.
        int minutos_viaje = 0;

        /// Minutos de espera entre partidos (la espera en casa del primer viaje
        /// no se cuenta: el árbitro sale más tarde).
        int minutos_espera = 0;

        /// Suma de la duración de los partidos cubiertos.
        int minutos_partido = 0;

        /// Tiempo desde la salida de casa hasta la vuelta, pasando por viajes,
        /// esperas y partidos. Puede incluir minutos del día anterior si el
        /// primer desplazamiento empieza antes de las 00:00.
        int minutos_totales = 0;

        /// Retribución por hora efectiva: `retribucion / (minutos_totales / 60)`.
        /// Es la métrica que interesa al objetivo del TFG (dinero por tiempo
        /// realmente dedicado, no solo por tiempo arbitrado).
        double euros_por_hora = 0.0;
    };

    /// Métricas agregadas de una jornada.
    struct MetricasJornada
    {
        /// Árbitros que trabajan, ordenados por retribución descendente y, a
        /// igualdad, por id ascendente.
        std::vector<MetricasArbitro> arbitros;

        /// Plazas del día (niveles del árbol).
        std::size_t plazas_totales = 0;

        /// Plazas cubiertas por algún árbitro.
        std::size_t plazas_cubiertas = 0;

        /// Árbitros compactados disponibles ese día.
        std::size_t arbitros_disponibles = 0;

        /// Árbitros con al menos una plaza.
        std::size_t arbitros_usados = 0;

        /// Retribución de las plazas cubiertas, en céntimos.
        std::int64_t retribucion_cubierta_centimos = 0;

        /// Retribución de las plazas que se quedaron vacías, en céntimos.
        std::int64_t retribucion_perdida_centimos = 0;

        /// Parte del coste que no es retribución perdida: viajes, esperas y el
        /// épsilon de cada asignación. Es `coste - retribucion_perdida`.
        std::int64_t coste_tiempo_centimos = 0;

        /// Sumas de los minutos de todos los árbitros del día.
        int minutos_viaje = 0;
        int minutos_espera = 0;
        int minutos_partido = 0;
        int minutos_totales = 0;

        /// Retribución cubierta por hora efectiva del conjunto del día.
        double euros_por_hora = 0.0;
    };

    /// Reconstruye las métricas de una jornada a partir de su solución.
    ///
    /// La solución debe tener exactamente `dia.n_plazas()` decisiones y sus
    /// `arbitro_global` deben existir en `dia.arbitros`. El recorrido de cada
    /// árbitro respeta el modelo de la función objetivo:
    ///
    ///  - el primer partido se coge desde el domicilio y se sale en
    ///    `inicio - viaje` (la espera en casa no se cobra);
    ///  - los siguientes se cogen desde la sede del anterior, con
    ///    `espera = inicio - (liberación + viaje)`;
    ///  - al final se vuelve al domicilio.
    ///
    /// @param dia Instancia del día con matriz, sedes compactas y plazas.
    /// @param solucion Solución reconstruida por `reconstruir_solucion`.
    MetricasJornada calcular_metricas(const InstanciaDia &dia,
                                      const SolucionDia &solucion);

} // namespace referee_scheduler::algorithms
