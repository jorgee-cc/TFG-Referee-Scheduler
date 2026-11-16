// Implementación de la orquestación por jornadas.
//
// Se construyen los datos estáticos de cada día con `construir_instancia_dia`
// (fases 1-5), se resuelven los casos que no necesitan árbol y, para el resto,
// se crea una `Arena` del tamaño exacto del día y se llama a `buscar_dijkstra`.
// Después se reconstruyen las métricas por árbitro con la solución y la matriz.

#include "referee_scheduler/algorithms/orquestacion.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace referee_scheduler::algorithms
{

    namespace
    {

        /// Solución en la que todas las plazas quedan vacías.
        ///
        /// Es válida cuando no hay ninguna plaza principal (día sin plazas o
        /// solo con secundarias). El coste es la suma de las retribuciones que
        /// se pierden; el generador de hijos haría exactamente lo mismo.
        SolucionDia solucion_todas_vacias(const InstanciaDia &dia)
        {
            SolucionDia solucion;
            solucion.coste = 0;
            solucion.decisiones.reserve(dia.n_plazas());

            for (const PlazaDia &plaza : dia.plazas)
            {
                assert(!plaza.es_principal() &&
                       "no se puede dejar vacia una plaza principal");

                DecisionDia decision;
                decision.plaza_id = plaza.plaza_id;
                decision.partido_id = plaza.partido_id;
                decision.arbitro_global = -1;
                solucion.decisiones.push_back(decision);

                solucion.coste += plaza.retribucion_centimos;
            }

            return solucion;
        }

        /// Marca una búsqueda sin solución (sin crear arena).
        ResultadoBusqueda sin_solucion()
        {
            ResultadoBusqueda resultado;
            resultado.causa = CausaParada::SinSolucion;
            resultado.hay_solucion = false;
            return resultado;
        }

        /// Convierte la solución reconstruida en las asignaciones del informe.
        std::vector<AsignacionResumen>
        construir_asignaciones(const InstanciaDia &dia,
                               const SolucionDia &solucion)
        {
            assert(solucion.decisiones.size() == dia.n_plazas());

            std::vector<AsignacionResumen> asignaciones;
            asignaciones.reserve(dia.n_plazas());

            for (std::size_t nivel = 0; nivel < dia.n_plazas(); ++nivel)
            {
                const PlazaDia &plaza = dia.plazas[nivel];
                const DecisionDia &decision = solucion.decisiones[nivel];

                AsignacionResumen asignacion;
                asignacion.plaza_id = plaza.plaza_id;
                asignacion.partido_id = plaza.partido_id;
                asignacion.arbitro_global = decision.arbitro_global;
                asignacion.rol = plaza.rol;
                asignacion.retribucion_centimos = plaza.retribucion_centimos;
                asignaciones.push_back(asignacion);
            }

            return asignaciones;
        }

        /// Suma las métricas de un día en el acumulado global de su árbitro.
        void acumular_arbitro(std::map<int, MetricasArbitro> &globales,
                              const MetricasArbitro &dia)
        {
            MetricasArbitro &global = globales[dia.arbitro_global];
            global.arbitro_global = dia.arbitro_global;
            global.nivel = dia.nivel;
            global.n_plazas += dia.n_plazas;
            global.retribucion_centimos += dia.retribucion_centimos;
            global.n_desplazamientos += dia.n_desplazamientos;
            global.minutos_viaje += dia.minutos_viaje;
            global.minutos_espera += dia.minutos_espera;
            global.minutos_partido += dia.minutos_partido;
            global.minutos_totales += dia.minutos_totales;
        }

    } // namespace

    ResumenEjecucion resolver_instancia(
        const domain::Instancia &instancia,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones_distancias,
        const ParametrosObjetivo &parametros,
        const OpcionesBusqueda &opciones_busqueda)
    {
        ResumenEjecucion resumen;
        resumen.arbitros_en_instancia = instancia.arbitros.size();

        const std::vector<GrupoDia> grupos = agrupar_por_dia(instancia);
        resumen.jornadas.reserve(grupos.size());

        // Acumulado por id global para el resumen de todas las jornadas.
        std::map<int, MetricasArbitro> arbitros_globales;

        for (const GrupoDia &grupo : grupos)
        {
            const auto inicio_construccion = std::chrono::steady_clock::now();
            InstanciaDia dia = construir_instancia_dia(
                instancia, grupo, proveedor, opciones_distancias);
            const auto fin_construccion = std::chrono::steady_clock::now();

            ResultadoJornada jornada;
            jornada.dia = grupo.dia;
            jornada.n_arbitros = dia.n_arbitros();
            jornada.n_plazas = dia.n_plazas();
            jornada.milisegundos_construccion =
                std::chrono::duration<double, std::milli>(
                    fin_construccion - inicio_construccion)
                    .count();

            // Capacidad del estado empaquetado: es un límite de compilación,
            // no un dato de la instancia. Se comprueba aquí para dar un error
            // claro en lugar de dejar que la arena reviente en un `assert`.
            if (dia.n_arbitros() > kMaxArbitros)
            {
                throw std::runtime_error(
                    "una jornada tiene " + std::to_string(dia.n_arbitros()) +
                    " árbitros y el máximo del estado empaquetado es " +
                    std::to_string(kMaxArbitros));
            }

            if (dia.n_plazas() == 0)
            {
                // Día sin plazas: la raíz ya sería meta y el coste es 0. No se
                // crea arena (podría no haber árbitros).
                jornada.busqueda.causa = CausaParada::Optima;
                jornada.busqueda.hay_solucion = true;
                jornada.busqueda.coste = 0;
                jornada.busqueda.solucion = SolucionDia{};
            }
            else if (dia.n_arbitros() == 0)
            {
                // Sin árbitros solo se puede resolver si no hay principales.
                if (tiene_solucion_estatica(dia))
                {
                    SolucionDia solucion = solucion_todas_vacias(dia);
                    jornada.busqueda.causa = CausaParada::Optima;
                    jornada.busqueda.hay_solucion = true;
                    jornada.busqueda.coste = solucion.coste;
                    jornada.busqueda.solucion = std::move(solucion);
                }
                else
                {
                    jornada.busqueda = sin_solucion();
                }
            }
            else if (!tiene_solucion_estatica(dia))
            {
                // Condición necesaria: una principal sin candidatos no se puede
                // cubrir nunca. Se evita construir la arena y explorar un árbol
                // que moriría en el primer nivel.
                jornada.busqueda = sin_solucion();
            }
            else
            {
                Arena arena(dia.n_arbitros());
                jornada.busqueda = buscar_dijkstra(
                    arena, dia, parametros, opciones_busqueda);
            }

            if (jornada.busqueda.solucion)
            {
                jornada.metricas =
                    calcular_metricas(dia, *jornada.busqueda.solucion);
                jornada.asignaciones = construir_asignaciones(
                    dia, *jornada.busqueda.solucion);
            }
            else
            {
                // Sin solución no hay recorrido que reconstruir, pero sí se
                // conocen los tamaños del día para el informe.
                jornada.metricas.plazas_totales = dia.n_plazas();
                jornada.metricas.arbitros_disponibles = dia.n_arbitros();
            }

            // --- Acumulación global ---
            resumen.coste_total_centimos +=
                jornada.busqueda.hay_solucion ? jornada.busqueda.coste : 0;

            resumen.plazas_totales += jornada.metricas.plazas_totales;
            resumen.plazas_cubiertas += jornada.metricas.plazas_cubiertas;
            resumen.retribucion_cubierta_centimos +=
                jornada.metricas.retribucion_cubierta_centimos;
            resumen.retribucion_perdida_centimos +=
                jornada.metricas.retribucion_perdida_centimos;

            resumen.minutos_viaje += jornada.metricas.minutos_viaje;
            resumen.minutos_espera += jornada.metricas.minutos_espera;
            resumen.minutos_partido += jornada.metricas.minutos_partido;
            resumen.minutos_totales += jornada.metricas.minutos_totales;

            resumen.milisegundos_construccion +=
                jornada.milisegundos_construccion;
            resumen.milisegundos_busqueda += jornada.busqueda.milisegundos;

            resumen.nodos_creados += jornada.busqueda.nodos_creados;
            resumen.nodos_expandidos += jornada.busqueda.nodos_expandidos;
            resumen.hijos_descartados += jornada.busqueda.hijos_descartados;
            resumen.extracciones_obsoletas +=
                jornada.busqueda.extracciones_obsoletas;
            resumen.estados_distintos += jornada.busqueda.estados_distintos;
            resumen.max_abierta =
                std::max(resumen.max_abierta, jornada.busqueda.max_abierta);

            const std::size_t memoria_dia = jornada.busqueda.memoria_arena +
                                            jornada.busqueda.memoria_abierta +
                                            jornada.busqueda.memoria_tabla;
            resumen.memoria_maxima =
                std::max(resumen.memoria_maxima, memoria_dia);

            resumen.hay_solucion =
                resumen.hay_solucion && jornada.busqueda.hay_solucion;
            resumen.todas_optimas =
                resumen.todas_optimas &&
                jornada.busqueda.causa == CausaParada::Optima;

            for (const MetricasArbitro &arbitro : jornada.metricas.arbitros)
            {
                acumular_arbitro(arbitros_globales, arbitro);
            }

            resumen.jornadas.push_back(std::move(jornada));
        }

        // Agregado de árbitros de todas las jornadas. Se recalcula la tarifa
        // efectiva porque sumar las tarifas diarias no es correcto (cada día
        // tiene su propio denominador).
        resumen.arbitros.reserve(arbitros_globales.size());
        for (auto &[id, arbitro] : arbitros_globales)
        {
            (void)id;
            if (arbitro.minutos_totales > 0)
            {
                arbitro.euros_por_hora =
                    (static_cast<double>(arbitro.retribucion_centimos) / 100.0) /
                    (static_cast<double>(arbitro.minutos_totales) / 60.0);
            }
            else
            {
                arbitro.euros_por_hora = 0.0;
            }
            resumen.arbitros.push_back(std::move(arbitro));
        }

        resumen.arbitros_usados = resumen.arbitros.size();

        std::sort(resumen.arbitros.begin(), resumen.arbitros.end(),
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

        resumen.milisegundos_total =
            resumen.milisegundos_construccion + resumen.milisegundos_busqueda;

        if (resumen.minutos_totales > 0)
        {
            resumen.euros_por_hora =
                (static_cast<double>(resumen.retribucion_cubierta_centimos) /
                 100.0) /
                (static_cast<double>(resumen.minutos_totales) / 60.0);
        }

        return resumen;
    }

} // namespace referee_scheduler::algorithms
