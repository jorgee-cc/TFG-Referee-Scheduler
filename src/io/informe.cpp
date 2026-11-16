// Implementación de los informes: resumen de terminal y JSON completo.
//
// El resumen de texto no lista asignaciones ni árbitros: con 3.000 plazas la
// terminal dejaría de ser útil. Muestra el coste, su desglose, los nodos, los
// tiempos y los minutos; el detalle completo va en el JSON opcional.

#include "referee_scheduler/io/informe.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/io/json.hpp"
#include "referee_scheduler/io/roles.hpp"

namespace referee_scheduler::io
{

    namespace
    {

        /// Agrupa los dígitos de un entero con puntos de millar.
        std::string agrupar_digitos(std::string digitos)
        {
            for (std::size_t i = digitos.size(); i > 3; i -= 3)
            {
                digitos.insert(i - 3, ".");
            }
            return digitos;
        }

        std::string entero_con_puntos(std::uint64_t valor)
        {
            return agrupar_digitos(std::to_string(valor));
        }

        std::string entero_con_puntos(std::int64_t valor)
        {
            if (valor < 0)
            {
                // Se evita el desbordamiento de `-INT64_MIN`.
                const std::uint64_t magnitud =
                    static_cast<std::uint64_t>(-(valor + 1)) + 1;
                return "-" + entero_con_puntos(magnitud);
            }
            return entero_con_puntos(static_cast<std::uint64_t>(valor));
        }

        /// Formatea un decimal con separador de miles y coma decimal.
        std::string decimal_con_comas(double valor, int decimales)
        {
            const bool negativo = valor < 0.0;
            if (negativo)
            {
                valor = -valor;
            }

            std::ostringstream flujo;
            flujo << std::fixed << std::setprecision(decimales) << valor;
            std::string texto = flujo.str();

            const std::size_t punto = texto.find('.');
            std::string parte_entera =
                punto == std::string::npos ? texto : texto.substr(0, punto);
            const std::string parte_decimal =
                punto == std::string::npos ? "" : texto.substr(punto + 1);

            std::string resultado = agrupar_digitos(parte_entera);
            if (!parte_decimal.empty())
            {
                resultado += ",";
                resultado += parte_decimal;
            }
            if (negativo)
            {
                resultado.insert(resultado.begin(), '-');
            }
            return resultado;
        }

        /// Formatea una cantidad de céntimos como euros.
        std::string euros(std::int64_t centimos)
        {
            const bool negativo = centimos < 0;
            const std::uint64_t magnitud =
                negativo ? static_cast<std::uint64_t>(-(centimos + 1)) + 1
                         : static_cast<std::uint64_t>(centimos);

            const std::uint64_t parte_entera = magnitud / 100;
            const std::uint64_t parte_decimal = magnitud % 100;

            std::ostringstream flujo;
            if (negativo)
            {
                flujo << '-';
            }
            flujo << entero_con_puntos(parte_entera) << ','
                  << std::setfill('0') << std::setw(2) << parte_decimal
                  << " €";
            return flujo.str();
        }

        std::string euros_por_hora(double valor)
        {
            return decimal_con_comas(valor, 2) + " €/h";
        }

        std::string milisegundos(double valor)
        {
            return decimal_con_comas(valor, 1) + " ms";
        }

        std::string bytes_humanos(std::size_t bytes)
        {
            constexpr double kKib = 1024.0;
            constexpr double kMib = 1024.0 * 1024.0;

            if (static_cast<double>(bytes) >= kMib)
            {
                return decimal_con_comas(static_cast<double>(bytes) / kMib, 1) +
                       " MB";
            }
            if (static_cast<double>(bytes) >= kKib)
            {
                return decimal_con_comas(static_cast<double>(bytes) / kKib, 1) +
                       " KB";
            }
            return entero_con_puntos(bytes) + " B";
        }

        std::string porcentaje(std::size_t parte, std::size_t total)
        {
            if (total == 0)
            {
                return "0,0 %";
            }
            return decimal_con_comas(
                       100.0 * static_cast<double>(parte) /
                           static_cast<double>(total),
                       1) +
                   " %";
        }

        std::string nodos_por_segundo(std::size_t nodos, double milisegundos)
        {
            if (milisegundos <= 0.0)
            {
                return "0,0";
            }
            return decimal_con_comas(
                static_cast<double>(nodos) * 1000.0 / milisegundos, 1);
        }

        std::string fecha_texto(algorithms::DiaCivil dia)
        {
            std::ostringstream flujo;
            flujo << std::setfill('0') << std::setw(4) << dia.year << '-'
                  << std::setw(2) << dia.mes << '-' << std::setw(2) << dia.dia;
            return flujo.str();
        }

        std::string dia_semana(algorithms::DiaCivil dia)
        {
            // 1970-01-01 fue jueves; `indice_dia` cuenta días desde esa fecha.
            static constexpr const char *nombres[] = {
                "jueves", "viernes", "sábado", "domingo",
                "lunes", "martes", "miércoles"};
            const std::int64_t indice = algorithms::indice_dia(dia);
            const std::size_t posicion =
                static_cast<std::size_t>(((indice % 7) + 7) % 7);
            return nombres[posicion];
        }

        const char *causa_texto(algorithms::CausaParada causa) noexcept
        {
            switch (causa)
            {
            case algorithms::CausaParada::Optima:
                return "óptima";
            case algorithms::CausaParada::SinSolucion:
                return "sin solución";
            case algorithms::CausaParada::PresupuestoAgotado:
                return "presupuesto agotado";
            }
            return "desconocida";
        }

        const char *causa_json(algorithms::CausaParada causa) noexcept
        {
            switch (causa)
            {
            case algorithms::CausaParada::Optima:
                return "optima";
            case algorithms::CausaParada::SinSolucion:
                return "sin_solucion";
            case algorithms::CausaParada::PresupuestoAgotado:
                return "presupuesto_agotado";
            }
            return "desconocida";
        }

        const char *bool_json(bool valor) noexcept
        {
            return valor ? "true" : "false";
        }

        /// Número para JSON: sin separador de miles y con punto decimal.
        std::string numero_json(double valor, int decimales)
        {
            std::ostringstream flujo;
            flujo << std::fixed << std::setprecision(decimales) << valor;
            return flujo.str();
        }

        void escribir_arbitro_json(std::ostream &salida,
                                   const algorithms::MetricasArbitro &arbitro)
        {
            salida << "        {\n";
            salida << "          \"arbitro_id\": " << arbitro.arbitro_global
                   << ",\n";
            salida << "          \"nivel\": " << arbitro.nivel << ",\n";
            salida << "          \"n_plazas\": " << arbitro.n_plazas << ",\n";
            salida << "          \"retribucion_centimos\": "
                   << arbitro.retribucion_centimos << ",\n";
            salida << "          \"n_desplazamientos\": "
                   << arbitro.n_desplazamientos << ",\n";
            salida << "          \"minutos_viaje\": " << arbitro.minutos_viaje
                   << ",\n";
            salida << "          \"minutos_espera\": " << arbitro.minutos_espera
                   << ",\n";
            salida << "          \"minutos_partido\": " << arbitro.minutos_partido
                   << ",\n";
            salida << "          \"minutos_totales\": " << arbitro.minutos_totales
                   << ",\n";
            salida << "          \"euros_por_hora\": "
                   << numero_json(arbitro.euros_por_hora, 2) << "\n";
            salida << "        }";
        }

    } // namespace

    void escribir_resumen_texto(std::ostream &salida,
                                const algorithms::ResumenEjecucion &resumen,
                                std::string_view ruta_instancia)
    {
        salida << "Instancia: " << ruta_instancia << '\n';
        salida << "Jornadas: " << resumen.jornadas.size()
               << "   Plazas: " << entero_con_puntos(resumen.plazas_totales)
               << "   Árbitros en la instancia: "
               << entero_con_puntos(resumen.arbitros_en_instancia) << "\n\n";

        for (const algorithms::ResultadoJornada &jornada : resumen.jornadas)
        {
            const algorithms::MetricasJornada &metricas = jornada.metricas;
            const bool hay_solucion = jornada.busqueda.hay_solucion;
            const std::size_t vacias =
                metricas.plazas_totales - metricas.plazas_cubiertas;

            salida << "Jornada " << fecha_texto(jornada.dia) << " ("
                   << dia_semana(jornada.dia) << ") — "
                   << causa_texto(jornada.busqueda.causa) << '\n';

            salida << "  Plazas: " << metricas.plazas_totales << " ("
                   << metricas.plazas_cubiertas << " cubiertas, " << vacias
                   << " vacías)   Árbitros usados: "
                   << metricas.arbitros_usados << "/"
                   << metricas.arbitros_disponibles << '\n';

            if (hay_solucion)
            {
                salida << "  Coste: " << euros(jornada.busqueda.coste) << " ("
                       << entero_con_puntos(jornada.busqueda.coste)
                       << " céntimos)\n";
                salida << "    Coste de tiempo (viajes + esperas + épsilon): "
                       << euros(metricas.coste_tiempo_centimos) << '\n';
                salida << "    Retribución perdida: "
                       << euros(metricas.retribucion_perdida_centimos) << '\n';
                salida << "    Retribución cubierta: "
                       << euros(metricas.retribucion_cubierta_centimos) << '\n';
            }
            else
            {
                salida << "  Coste: — (sin solución)\n";
            }

            const double ms_total = jornada.milisegundos_construccion +
                                    jornada.busqueda.milisegundos;
            salida << "  Tiempo: construcción "
                   << milisegundos(jornada.milisegundos_construccion)
                   << " + búsqueda " << milisegundos(jornada.busqueda.milisegundos)
                   << " = " << milisegundos(ms_total) << '\n';

            salida << "  Nodos: "
                   << entero_con_puntos(jornada.busqueda.nodos_creados)
                   << " creados, "
                   << entero_con_puntos(jornada.busqueda.nodos_expandidos)
                   << " expandidos, "
                   << entero_con_puntos(jornada.busqueda.hijos_descartados)
                   << " hijos descartados\n";
            salida << "  Nodos/s: "
                   << nodos_por_segundo(jornada.busqueda.nodos_expandidos,
                                        jornada.busqueda.milisegundos)
                   << "   Estados distintos: "
                   << entero_con_puntos(jornada.busqueda.estados_distintos)
                   << "   Máx. lista abierta: "
                   << entero_con_puntos(jornada.busqueda.max_abierta) << '\n';

            salida << "  Memoria: arena "
                   << bytes_humanos(jornada.busqueda.memoria_arena)
                   << " + abierta "
                   << bytes_humanos(jornada.busqueda.memoria_abierta)
                   << " + tabla "
                   << bytes_humanos(jornada.busqueda.memoria_tabla) << '\n';

            salida << "  Desplazamientos: " << metricas.minutos_viaje
                   << " min de viaje, " << metricas.minutos_espera
                   << " min de espera, " << metricas.minutos_partido
                   << " min de arbitraje\n";
            salida << "  Tarifa efectiva de la jornada: "
                   << euros_por_hora(metricas.euros_por_hora) << "\n\n";
        }

        salida << "Resumen global\n";
        salida << "  Plazas cubiertas: " << resumen.plazas_cubiertas << "/"
               << resumen.plazas_totales << " ("
               << porcentaje(resumen.plazas_cubiertas, resumen.plazas_totales)
               << ")   Árbitros usados: " << resumen.arbitros_usados << "/"
               << resumen.arbitros_en_instancia << '\n';

        salida << "  Coste "
               << (resumen.hay_solucion ? "total" : "de las jornadas con solución")
               << ": " << euros(resumen.coste_total_centimos) << '\n';

        salida << "  Viajes: " << resumen.minutos_viaje
               << " min   Esperas: " << resumen.minutos_espera
               << " min   Arbitraje: " << resumen.minutos_partido
               << " min   Tiempo dedicado: " << resumen.minutos_totales
               << " min\n";

        salida << "  Tiempo total: construcción "
               << milisegundos(resumen.milisegundos_construccion)
               << " + búsqueda " << milisegundos(resumen.milisegundos_busqueda)
               << " = " << milisegundos(resumen.milisegundos_total) << '\n';
        salida << "  Nodos/s global: "
               << nodos_por_segundo(resumen.nodos_expandidos,
                                    resumen.milisegundos_busqueda)
               << "   Estados distintos: "
               << entero_con_puntos(resumen.estados_distintos)
               << "   Memoria máxima: "
               << bytes_humanos(resumen.memoria_maxima) << '\n';
        salida << "  Tarifa efectiva global: "
               << euros_por_hora(resumen.euros_por_hora) << '\n';

        if (!resumen.todas_optimas)
        {
            salida << "  Aviso: alguna jornada no está demostrada óptima "
                      "(presupuesto agotado o sin solución).\n";
        }
    }

    void escribir_informe_json(std::ostream &salida,
                               const algorithms::ResumenEjecucion &resumen,
                               std::string_view ruta_instancia)
    {
        salida << "{\n";
        salida << "  \"instancia\": " << comillas_json(ruta_instancia) << ",\n";
        salida << "  \"hay_solucion\": " << bool_json(resumen.hay_solucion)
               << ",\n";
        salida << "  \"todas_optimas\": " << bool_json(resumen.todas_optimas)
               << ",\n";
        salida << "  \"coste_total_centimos\": " << resumen.coste_total_centimos
               << ",\n";
        salida << "  \"plazas_totales\": " << resumen.plazas_totales << ",\n";
        salida << "  \"plazas_cubiertas\": " << resumen.plazas_cubiertas << ",\n";
        salida << "  \"arbitros_en_instancia\": "
               << resumen.arbitros_en_instancia << ",\n";
        salida << "  \"arbitros_usados\": " << resumen.arbitros_usados << ",\n";
        salida << "  \"retribucion_cubierta_centimos\": "
               << resumen.retribucion_cubierta_centimos << ",\n";
        salida << "  \"retribucion_perdida_centimos\": "
               << resumen.retribucion_perdida_centimos << ",\n";
        salida << "  \"minutos_viaje\": " << resumen.minutos_viaje << ",\n";
        salida << "  \"minutos_espera\": " << resumen.minutos_espera << ",\n";
        salida << "  \"minutos_partido\": " << resumen.minutos_partido << ",\n";
        salida << "  \"minutos_totales\": " << resumen.minutos_totales << ",\n";
        salida << "  \"euros_por_hora\": "
               << numero_json(resumen.euros_por_hora, 2) << ",\n";
        salida << "  \"milisegundos_construccion\": "
               << numero_json(resumen.milisegundos_construccion, 3) << ",\n";
        salida << "  \"milisegundos_busqueda\": "
               << numero_json(resumen.milisegundos_busqueda, 3) << ",\n";
        salida << "  \"milisegundos_total\": "
               << numero_json(resumen.milisegundos_total, 3) << ",\n";
        salida << "  \"nodos_creados\": " << resumen.nodos_creados << ",\n";
        salida << "  \"nodos_expandidos\": " << resumen.nodos_expandidos << ",\n";
        salida << "  \"hijos_descartados\": " << resumen.hijos_descartados
               << ",\n";
        salida << "  \"extracciones_obsoletas\": "
               << resumen.extracciones_obsoletas << ",\n";
        salida << "  \"estados_distintos\": " << resumen.estados_distintos
               << ",\n";
        salida << "  \"max_abierta\": " << resumen.max_abierta << ",\n";
        salida << "  \"memoria_maxima\": " << resumen.memoria_maxima << ",\n";

        salida << "  \"jornadas\": [";
        if (resumen.jornadas.empty())
        {
            salida << "],\n";
        }
        else
        {
            salida << '\n';
            for (std::size_t i = 0; i < resumen.jornadas.size(); ++i)
            {
                const algorithms::ResultadoJornada &jornada =
                    resumen.jornadas[i];
                const algorithms::MetricasJornada &metricas =
                    jornada.metricas;

                salida << "    {\n";
                salida << "      \"fecha\": " << comillas_json(fecha_texto(jornada.dia))
                       << ",\n";
                salida << "      \"causa\": "
                       << comillas_json(causa_json(jornada.busqueda.causa))
                       << ",\n";
                salida << "      \"hay_solucion\": "
                       << bool_json(jornada.busqueda.hay_solucion) << ",\n";

                if (jornada.busqueda.hay_solucion)
                {
                    salida << "      \"coste_centimos\": "
                           << jornada.busqueda.coste << ",\n";
                    salida << "      \"coste_tiempo_centimos\": "
                           << metricas.coste_tiempo_centimos << ",\n";
                }
                else
                {
                    salida << "      \"coste_centimos\": null,\n";
                    salida << "      \"coste_tiempo_centimos\": null,\n";
                }

                salida << "      \"retribucion_cubierta_centimos\": "
                       << metricas.retribucion_cubierta_centimos << ",\n";
                salida << "      \"retribucion_perdida_centimos\": "
                       << metricas.retribucion_perdida_centimos << ",\n";
                salida << "      \"plazas_totales\": " << metricas.plazas_totales
                       << ",\n";
                salida << "      \"plazas_cubiertas\": "
                       << metricas.plazas_cubiertas << ",\n";
                salida << "      \"arbitros_disponibles\": "
                       << metricas.arbitros_disponibles << ",\n";
                salida << "      \"arbitros_usados\": " << metricas.arbitros_usados
                       << ",\n";
                salida << "      \"minutos_viaje\": " << metricas.minutos_viaje
                       << ",\n";
                salida << "      \"minutos_espera\": " << metricas.minutos_espera
                       << ",\n";
                salida << "      \"minutos_partido\": " << metricas.minutos_partido
                       << ",\n";
                salida << "      \"minutos_totales\": " << metricas.minutos_totales
                       << ",\n";
                salida << "      \"euros_por_hora\": "
                       << numero_json(metricas.euros_por_hora, 2) << ",\n";

                salida << "      \"milisegundos_construccion\": "
                       << numero_json(jornada.milisegundos_construccion, 3)
                       << ",\n";
                salida << "      \"milisegundos_busqueda\": "
                       << numero_json(jornada.busqueda.milisegundos, 3)
                       << ",\n";
                salida << "      \"nodos_creados\": "
                       << jornada.busqueda.nodos_creados << ",\n";
                salida << "      \"nodos_expandidos\": "
                       << jornada.busqueda.nodos_expandidos << ",\n";
                salida << "      \"hijos_descartados\": "
                       << jornada.busqueda.hijos_descartados << ",\n";
                salida << "      \"extracciones_obsoletas\": "
                       << jornada.busqueda.extracciones_obsoletas << ",\n";
                salida << "      \"estados_distintos\": "
                       << jornada.busqueda.estados_distintos << ",\n";
                salida << "      \"max_abierta\": "
                       << jornada.busqueda.max_abierta << ",\n";
                salida << "      \"nodos_por_segundo\": "
                       << numero_json(
                              jornada.busqueda.milisegundos > 0.0
                                  ? static_cast<double>(
                                        jornada.busqueda.nodos_expandidos) *
                                        1000.0 /
                                        jornada.busqueda.milisegundos
                                  : 0.0,
                              1)
                       << ",\n";
                salida << "      \"memoria_arena\": "
                       << jornada.busqueda.memoria_arena << ",\n";
                salida << "      \"memoria_abierta\": "
                       << jornada.busqueda.memoria_abierta << ",\n";
                salida << "      \"memoria_tabla\": "
                       << jornada.busqueda.memoria_tabla << ",\n";

                salida << "      \"asignaciones\": [";
                if (jornada.asignaciones.empty())
                {
                    salida << "],\n";
                }
                else
                {
                    salida << '\n';
                    for (std::size_t a = 0; a < jornada.asignaciones.size(); ++a)
                    {
                        const algorithms::AsignacionResumen &asignacion =
                            jornada.asignaciones[a];
                        salida << "        {\"plaza_id\": "
                               << asignacion.plaza_id
                               << ", \"partido_id\": " << asignacion.partido_id
                               << ", \"rol\": "
                               << comillas_json(texto_de_rol(asignacion.rol))
                               << ", \"arbitro_id\": ";
                        if (asignacion.cubierta())
                        {
                            salida << asignacion.arbitro_global;
                        }
                        else
                        {
                            salida << "null";
                        }
                        salida << ", \"retribucion_centimos\": "
                               << asignacion.retribucion_centimos
                               << ", \"cubierta\": "
                               << bool_json(asignacion.cubierta()) << "}";
                        if (a + 1 < jornada.asignaciones.size())
                        {
                            salida << ',';
                        }
                        salida << '\n';
                    }
                    salida << "      ],\n";
                }

                salida << "      \"arbitros\": [";
                if (metricas.arbitros.empty())
                {
                    salida << "]\n";
                }
                else
                {
                    salida << '\n';
                    for (std::size_t a = 0; a < metricas.arbitros.size(); ++a)
                    {
                        escribir_arbitro_json(salida, metricas.arbitros[a]);
                        if (a + 1 < metricas.arbitros.size())
                        {
                            salida << ',';
                        }
                        salida << '\n';
                    }
                    salida << "      ]\n";
                }

                salida << "    }";
                if (i + 1 < resumen.jornadas.size())
                {
                    salida << ',';
                }
                salida << '\n';
            }
            salida << "  ],\n";
        }

        salida << "  \"arbitros_globales\": [";
        if (resumen.arbitros.empty())
        {
            salida << "]\n";
        }
        else
        {
            salida << '\n';
            for (std::size_t i = 0; i < resumen.arbitros.size(); ++i)
            {
                escribir_arbitro_json(salida, resumen.arbitros[i]);
                if (i + 1 < resumen.arbitros.size())
                {
                    salida << ',';
                }
                salida << '\n';
            }
            salida << "  ]\n";
        }

        salida << "}\n";
    }

} // namespace referee_scheduler::io
