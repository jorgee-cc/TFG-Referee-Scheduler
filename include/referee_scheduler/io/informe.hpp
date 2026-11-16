// Informes de una ejecución: resumen legible por terminal y JSON completo.
//
// El resumen de texto está pensado para mirar una ejecución de un vistazo:
// coste, nodos, tiempos y minutos. Deliberadamente NO lista las asignaciones ni
// la tabla de árbitros: una instancia con miles de plazas convertiría la
// terminal en un volcado inútil. Esos datos están en el JSON, que se escribe
// solo cuando el usuario lo pide con `--salida`.
//
// El JSON incluye todas las asignaciones y las métricas de todos los árbitros
// que trabajan, para poder validar la solución o generar tablas y gráficas
// después.

#pragma once

#include <iosfwd>
#include <string_view>

#include "referee_scheduler/algorithms/orquestacion.hpp"

namespace referee_scheduler::io
{

    /// Escribe el resumen legible de la ejecución.
    ///
    /// @param salida Flujo de salida (normalmente `std::cout`).
    /// @param resumen Resultado agregado de todas las jornadas.
    /// @param ruta_instancia Ruta del fichero de entrada, solo para la cabecera.
    void escribir_resumen_texto(std::ostream &salida,
                                const algorithms::ResumenEjecucion &resumen,
                                std::string_view ruta_instancia);

    /// Escribe el informe completo en JSON.
    ///
    /// Incluye una entrada por jornada con su causa, coste, desglose de dinero,
    /// métricas de búsqueda, todas las asignaciones y las métricas de los
    /// árbitros que trabajan; al final, el agregado de árbitros de todas las
    /// jornadas.
    void escribir_informe_json(std::ostream &salida,
                               const algorithms::ResumenEjecucion &resumen,
                               std::string_view ruta_instancia);

} // namespace referee_scheduler::io
