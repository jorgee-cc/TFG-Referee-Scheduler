// Orquestación de una instancia completa: de `domain::Instancia` al resultado
// de todas las jornadas.
//
// La ejecución es por día (fases 1-5 independientes) y este módulo es el que
// decide qué hacer con los casos triviales antes de crear una `Arena`:
//  - día sin plazas: solución vacía, coste 0;
//  - día sin árbitros y solo con plazas secundarias: se dejan todas vacías y se
//    paga su retribución;
//  - día sin árbitros con alguna principal, o principal sin candidatos
//    estáticos: `SinSolucion`;
//  - resto: `buscar_dijkstra`.
//
// No depende de OSRM: recibe un `ProveedorDistancias`, así que los tests pueden
// inyectar matrices fijas. Tampoco escribe informes: eso vive en `io/`.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "referee_scheduler/algorithms/busqueda.hpp"
#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/algorithms/metricas.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace referee_scheduler::algorithms
{

    /// Decisión de una plaza con los datos que necesita el informe.
    ///
    /// `SolucionDia` solo guarda ids; aquí se añaden el rol y la retribución de
    /// la plaza para no volver a cruzar la instancia original al escribir el
    /// JSON.
    struct AsignacionResumen
    {
        /// Id global de la plaza.
        int plaza_id = 0;

        /// Id global del partido.
        int partido_id = 0;

        /// Id global del árbitro, o -1 si la plaza quedó vacía.
        int arbitro_global = -1;

        /// Rol de la plaza.
        domain::Rol rol = domain::Rol::Principal;

        /// Retribución de la plaza en céntimos.
        std::int64_t retribucion_centimos = 0;

        /// ¿La plaza está cubierta?
        bool cubierta() const noexcept { return arbitro_global >= 0; }
    };

    /// Resultado de una jornada.
    struct ResultadoJornada
    {
        /// Fecha civil de la jornada.
        DiaCivil dia;

        /// Árbitros compactados del día.
        std::size_t n_arbitros = 0;

        /// Plazas del día.
        std::size_t n_plazas = 0;

        /// Resultado del motor (causa, coste, solución y métricas de búsqueda).
        ResultadoBusqueda busqueda;

        /// Métricas reconstruidas de la solución (vacías si no hay solución).
        MetricasJornada metricas;

        /// Una entrada por plaza, en orden cronológico, con rol y retribución.
        std::vector<AsignacionResumen> asignaciones;

        /// Tiempo de construcción de los datos estáticos del día (matriz
        /// incluida, por tanto puede incluir la latencia de OSRM).
        double milisegundos_construccion = 0.0;
    };

    /// Resultado agregado de toda la instancia.
    struct ResumenEjecucion
    {
        /// Una entrada por fecha, ordenadas de la más antigua a la más reciente.
        std::vector<ResultadoJornada> jornadas;

        /// Árbitros que trabajan en alguna jornada, agregados por id global.
        /// Solo aparecen los que tienen al menos una plaza.
        std::vector<MetricasArbitro> arbitros;

        /// Suma de los costes de las jornadas con solución, en céntimos.
        std::int64_t coste_total_centimos = 0;

        /// Plazas de todas las jornadas.
        std::size_t plazas_totales = 0;

        /// Plazas cubiertas en todas las jornadas.
        std::size_t plazas_cubiertas = 0;

        /// Árbitros distintos declarados en la instancia.
        std::size_t arbitros_en_instancia = 0;

        /// Árbitros distintos que cubren al menos una plaza.
        std::size_t arbitros_usados = 0;

        /// Retribución cubierta y perdida, en céntimos.
        std::int64_t retribucion_cubierta_centimos = 0;
        std::int64_t retribucion_perdida_centimos = 0;

        /// Sumas de minutos de todas las jornadas.
        int minutos_viaje = 0;
        int minutos_espera = 0;
        int minutos_partido = 0;
        int minutos_totales = 0;

        /// Retribución cubierta por hora efectiva del conjunto.
        double euros_por_hora = 0.0;

        /// Tiempos de pared: construcción de datos estáticos y búsqueda.
        double milisegundos_construccion = 0.0;
        double milisegundos_busqueda = 0.0;
        double milisegundos_total = 0.0;

        /// Métricas de búsqueda agregadas.
        std::size_t nodos_creados = 0;
        std::size_t nodos_expandidos = 0;
        std::size_t hijos_descartados = 0;
        std::size_t extracciones_obsoletas = 0;
        std::size_t estados_distintos = 0;
        std::size_t max_abierta = 0;

        /// Máximo de memoria comprometida por una jornada (arena + lista
        /// abierta + tabla). Las jornadas se ejecutan secuencialmente, así que
        /// es el pico del proceso, no la suma.
        std::size_t memoria_maxima = 0;

        /// ¿Todas las jornadas tienen solución?
        bool hay_solucion = true;

        /// ¿Todas las soluciones están demostradas óptimas?
        bool todas_optimas = true;
    };

    /// Construye y resuelve todas las jornadas de una instancia.
    ///
    /// @param instancia Instancia completa (viernes, sábado y domingo).
    /// @param proveedor Fuente de tiempos de viaje (OSRM o un proveedor fijo).
    /// @param opciones_distancias Cierre métrico de la matriz.
    /// @param parametros Pesos de la función objetivo.
    /// @param opciones_busqueda Presupuesto de nodos y anchura de cubos.
    ResumenEjecucion resolver_instancia(
        const domain::Instancia &instancia,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones_distancias = {},
        const ParametrosObjetivo &parametros = {},
        const OpcionesBusqueda &opciones_busqueda = {});

} // namespace referee_scheduler::algorithms
