// Implementación de las métricas de una jornada.
//
// Se recorre la solución en el mismo orden cronológico en que la construyó el
// árbol (el índice de la decisión es el nivel, y el nivel es la plaza) y se
// acumula el recorrido de cada árbitro. La matriz de distancias ya está cerrada
// métricamente y es asimétrica, así que se usa siempre en el sentido real:
// domicilio -> sede, sede -> sede y sede -> domicilio.

#include "referee_scheduler/algorithms/metricas.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace referee_scheduler::algorithms
{

    namespace
    {

        /// Acumulador interno del recorrido de un árbitro.
        struct Acumulado
        {
            bool usado = false;

            std::size_t n_plazas = 0;
            std::int64_t retribucion_centimos = 0;
            std::size_t n_desplazamientos = 0;

            int minutos_viaje = 0;
            int minutos_espera = 0;
            int minutos_partido = 0;

            /// Sede en la que queda el árbitro tras su última plaza.
            int sede_actual = 0;

            /// Minuto en que queda libre tras su última plaza.
            int minuto_libre = 0;

            /// Minuto en que salió de casa por primera vez.
            int salida = 0;

            /// ¿Todavía no ha salido de casa?
            bool en_casa = true;
        };

        double euros_por_hora(std::int64_t centimos, int minutos) noexcept
        {
            if (minutos <= 0)
            {
                return 0.0;
            }
            return (static_cast<double>(centimos) / 100.0) /
                   (static_cast<double>(minutos) / 60.0);
        }

    } // namespace

    MetricasJornada calcular_metricas(const InstanciaDia &dia,
                                      const SolucionDia &solucion)
    {
        assert(solucion.decisiones.size() == dia.n_plazas() &&
               "la solucion debe tener una decision por plaza");
        if (solucion.decisiones.size() != dia.n_plazas())
        {
            throw std::invalid_argument(
                "la solucion no tiene una decision por plaza");
        }

        MetricasJornada metricas;
        metricas.plazas_totales = dia.n_plazas();
        metricas.arbitros_disponibles = dia.n_arbitros();

        // El id global de cada árbitro compacto. `compactar_arbitros` ordena por
        // id global, pero el mapa evita depender de que el vector esté ordenado.
        std::unordered_map<int, std::size_t> indice_por_global;
        indice_por_global.reserve(dia.n_arbitros());
        for (std::size_t i = 0; i < dia.n_arbitros(); ++i)
        {
            indice_por_global.emplace(dia.arbitros[i].id_global, i);
        }

        std::vector<Acumulado> acumulados(dia.n_arbitros());

        for (std::size_t nivel = 0; nivel < dia.n_plazas(); ++nivel)
        {
            const PlazaDia &plaza = dia.plazas[nivel];
            const DecisionDia &decision = solucion.decisiones[nivel];

            if (decision.arbitro_global < 0)
            {
                // Plaza vacía: su retribución es la parte del coste que se
                // pierde. Las principales no pueden llegar aquí en una solución
                // válida, pero se contabilizan igual por robustez.
                metricas.retribucion_perdida_centimos +=
                    plaza.retribucion_centimos;
                continue;
            }

            const auto encontrado =
                indice_por_global.find(decision.arbitro_global);
            assert(encontrado != indice_por_global.end() &&
                   "la solucion referencia un arbitro que no esta en el dia");
            if (encontrado == indice_por_global.end())
            {
                throw std::invalid_argument(
                    "la solucion referencia un arbitro que no esta en el dia");
            }

            const std::size_t compacto = encontrado->second;
            const ArbitroDia &arbitro = dia.arbitros[compacto];
            Acumulado &acumulado = acumulados[compacto];

            // Primer trayecto: se sale del domicilio. Los siguientes salen de
            // la sede en la que quedó el árbitro tras su plaza anterior.
            const std::size_t origen = acumulado.en_casa
                                           ? static_cast<std::size_t>(arbitro.domicilio)
                                           : static_cast<std::size_t>(acumulado.sede_actual);
            const int viaje = dia.viaje(origen, plaza.sede);
            assert(viaje != kMinutosInalcanzable &&
                   "una plaza asignada debe tener ruta; el filtro de candidatos "
                   "ya la exigio");
            if (viaje == kMinutosInalcanzable)
            {
                throw std::invalid_argument(
                    "una plaza asignada no tiene ruta desde la sede del arbitro");
            }

            if (acumulado.en_casa)
            {
                // Puede salir antes de `primer_inicio`: la disponibilidad
                // limita la hora de inicio del partido, no la de salida.
                acumulado.salida = plaza.minuto_inicio - viaje;
                acumulado.en_casa = false;
            }
            else
            {
                const int llegada = acumulado.minuto_libre + viaje;
                const int espera = plaza.minuto_inicio - llegada;
                assert(espera >= 0 &&
                       "la solucion no puede asignar una plaza a la que no llega");
                if (espera < 0)
                {
                    throw std::invalid_argument(
                        "la solucion asigna una plaza a la que el arbitro no "
                        "llega a tiempo");
                }
                acumulado.minutos_espera += espera;
            }

            acumulado.minutos_viaje += viaje;
            acumulado.minutos_partido += plaza.minuto_fin - plaza.minuto_inicio;
            acumulado.n_desplazamientos += 1;
            acumulado.n_plazas += 1;
            acumulado.retribucion_centimos += plaza.retribucion_centimos;
            acumulado.sede_actual = plaza.sede;
            acumulado.minuto_libre = plaza.minuto_fin;
            acumulado.usado = true;

            metricas.plazas_cubiertas += 1;
            metricas.retribucion_cubierta_centimos +=
                plaza.retribucion_centimos;
        }

        // Vuelta a casa de cada árbitro que ha trabajado. Se hace después de
        // recorrer todas las plazas porque solo se conoce la última sede al
        // final.
        for (std::size_t i = 0; i < dia.n_arbitros(); ++i)
        {
            Acumulado &acumulado = acumulados[i];
            if (!acumulado.usado)
            {
                continue;
            }

            const ArbitroDia &arbitro = dia.arbitros[i];
            const int viaje_vuelta =
                dia.viaje(static_cast<std::size_t>(acumulado.sede_actual),
                          static_cast<std::size_t>(arbitro.domicilio));
            assert(viaje_vuelta != kMinutosInalcanzable &&
                   "la vuelta a casa debe existir; el filtro de candidatos ya "
                   "la exigio");
            if (viaje_vuelta == kMinutosInalcanzable)
            {
                throw std::invalid_argument(
                    "la vuelta a casa de un arbitro no tiene ruta");
            }

            acumulado.minutos_viaje += viaje_vuelta;
            acumulado.n_desplazamientos += 1;

            const int llegada_a_casa = acumulado.minuto_libre + viaje_vuelta;
            const int minutos_totales = llegada_a_casa - acumulado.salida;
            assert(minutos_totales >= 0 &&
                   "el recorrido de un arbitro no puede durar minutos negativos");
            if (minutos_totales < 0)
            {
                throw std::invalid_argument(
                    "el recorrido de un arbitro dura minutos negativos");
            }

            MetricasArbitro resumen;
            resumen.arbitro_global = arbitro.id_global;
            resumen.nivel = arbitro.nivel;
            resumen.n_plazas = acumulado.n_plazas;
            resumen.retribucion_centimos = acumulado.retribucion_centimos;
            resumen.n_desplazamientos = acumulado.n_desplazamientos;
            resumen.minutos_viaje = acumulado.minutos_viaje;
            resumen.minutos_espera = acumulado.minutos_espera;
            resumen.minutos_partido = acumulado.minutos_partido;
            resumen.minutos_totales = minutos_totales;
            resumen.euros_por_hora =
                euros_por_hora(resumen.retribucion_centimos,
                               resumen.minutos_totales);

            metricas.minutos_viaje += resumen.minutos_viaje;
            metricas.minutos_espera += resumen.minutos_espera;
            metricas.minutos_partido += resumen.minutos_partido;
            metricas.minutos_totales += resumen.minutos_totales;

            metricas.arbitros.push_back(resumen);
        }

        metricas.arbitros_usados = metricas.arbitros.size();

        // Coste de tiempo = coste total - retribución perdida. El resto son
        // viajes, esperas y el épsilon de cada asignación.
        metricas.coste_tiempo_centimos =
            solucion.coste - metricas.retribucion_perdida_centimos;

        metricas.euros_por_hora =
            euros_por_hora(metricas.retribucion_cubierta_centimos,
                           metricas.minutos_totales);

        // Orden estable para el informe: primero quien más gana y, a igualdad,
        // el id menor.
        std::sort(metricas.arbitros.begin(), metricas.arbitros.end(),
                  [](const MetricasArbitro &izquierda,
                     const MetricasArbitro &derecha)
                  {
                      if (izquierda.retribucion_centimos !=
                          derecha.retribucion_centimos)
                      {
                          return izquierda.retribucion_centimos >
                                 derecha.retribucion_centimos;
                      }
                      return izquierda.arbitro_global <
                             derecha.arbitro_global;
                  });

        return metricas;
    }

} // namespace referee_scheduler::algorithms
