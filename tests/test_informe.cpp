// Pruebas de los informes: el resumen de terminal (sin volcar asignaciones ni
// la tabla de árbitros) y el JSON completo (estructura y valores).
//
// Se construye un `ResumenEjecucion` a mano para no depender de OSRM ni de la
// búsqueda. El JSON se vuelve a parsear con el parser del proyecto: si el
// informe no es JSON válido, el test falla.
//
// Es el ejecutable `referee_scheduler_informe_tests` de CTest.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "referee_scheduler/algorithms/orquestacion.hpp"
#include "referee_scheduler/io/informe.hpp"
#include "referee_scheduler/io/json.hpp"

namespace
{

    using referee_scheduler::algorithms::AsignacionResumen;
    using referee_scheduler::algorithms::CausaParada;
    using referee_scheduler::algorithms::MetricasArbitro;
    using referee_scheduler::algorithms::MetricasJornada;
    using referee_scheduler::algorithms::ResultadoJornada;
    using referee_scheduler::algorithms::ResumenEjecucion;
    using referee_scheduler::domain::Rol;
    using referee_scheduler::io::parsear_json;

    int fallos = 0;

    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

    ResumenEjecucion construir_resumen()
    {
        MetricasArbitro arbitro;
        arbitro.arbitro_global = 7;
        arbitro.nivel = 2;
        arbitro.n_plazas = 1;
        arbitro.retribucion_centimos = 4500;
        arbitro.n_desplazamientos = 2;
        arbitro.minutos_viaje = 2;
        arbitro.minutos_espera = 0;
        arbitro.minutos_partido = 60;
        arbitro.minutos_totales = 62;
        arbitro.euros_por_hora = 43.55;

        MetricasJornada metricas;
        metricas.arbitros.push_back(arbitro);
        metricas.plazas_totales = 1;
        metricas.plazas_cubiertas = 1;
        metricas.arbitros_disponibles = 1;
        metricas.arbitros_usados = 1;
        metricas.retribucion_cubierta_centimos = 4500;
        metricas.retribucion_perdida_centimos = 0;
        metricas.coste_tiempo_centimos = 29;
        metricas.minutos_viaje = 2;
        metricas.minutos_espera = 0;
        metricas.minutos_partido = 60;
        metricas.minutos_totales = 62;
        metricas.euros_por_hora = 43.55;

        AsignacionResumen asignacion;
        asignacion.plaza_id = 1000;
        asignacion.partido_id = 100;
        asignacion.arbitro_global = 7;
        asignacion.rol = Rol::Principal;
        asignacion.retribucion_centimos = 4500;

        ResultadoJornada jornada;
        jornada.dia = {2026, 11, 20};
        jornada.n_arbitros = 1;
        jornada.n_plazas = 1;
        jornada.busqueda.causa = CausaParada::Optima;
        jornada.busqueda.hay_solucion = true;
        jornada.busqueda.coste = 29;
        jornada.busqueda.nodos_creados = 17;
        jornada.busqueda.nodos_expandidos = 7;
        jornada.busqueda.hijos_descartados = 3;
        jornada.busqueda.estados_distintos = 17;
        jornada.busqueda.max_abierta = 11;
        jornada.busqueda.memoria_arena = 65520;
        jornada.busqueda.memoria_abierta = 1024;
        jornada.busqueda.memoria_tabla = 512;
        jornada.busqueda.milisegundos = 2.0;
        jornada.milisegundos_construccion = 1.0;
        jornada.metricas = metricas;
        jornada.asignaciones.push_back(asignacion);

        ResumenEjecucion resumen;
        resumen.jornadas.push_back(jornada);
        resumen.arbitros.push_back(arbitro);
        resumen.coste_total_centimos = 29;
        resumen.plazas_totales = 1;
        resumen.plazas_cubiertas = 1;
        resumen.arbitros_en_instancia = 1;
        resumen.arbitros_usados = 1;
        resumen.retribucion_cubierta_centimos = 4500;
        resumen.retribucion_perdida_centimos = 0;
        resumen.minutos_viaje = 2;
        resumen.minutos_espera = 0;
        resumen.minutos_partido = 60;
        resumen.minutos_totales = 62;
        resumen.euros_por_hora = 43.55;
        resumen.milisegundos_construccion = 1.0;
        resumen.milisegundos_busqueda = 2.0;
        resumen.milisegundos_total = 3.0;
        resumen.nodos_creados = 17;
        resumen.nodos_expandidos = 7;
        resumen.hijos_descartados = 3;
        resumen.estados_distintos = 17;
        resumen.max_abierta = 11;
        resumen.memoria_maxima = 67056;
        resumen.hay_solucion = true;
        resumen.todas_optimas = true;
        return resumen;
    }

} // namespace

int main()
{
    const ResumenEjecucion resumen = construir_resumen();

    std::ostringstream texto;
    referee_scheduler::io::escribir_resumen_texto(texto, resumen, "ejemplo.json");
    const std::string resumen_texto = texto.str();

    comprobar(resumen_texto.find("Instancia: ejemplo.json") != std::string::npos,
              "el resumen incluye la instancia");
    comprobar(resumen_texto.find("Coste: 0,29 €") != std::string::npos,
              "el resumen incluye el coste");
    comprobar(resumen_texto.find("Nodos/s") != std::string::npos,
              "el resumen incluye nodos/s");
    comprobar(resumen_texto.find("Nodos: 17 creados, 7 expandidos") !=
                  std::string::npos,
              "el resumen incluye nodos creados/expandidos");
    comprobar(resumen_texto.find("Resumen global") != std::string::npos,
              "el resumen incluye el bloque global");
    // El terminal no debe volcar asignaciones ni la tabla de árbitros.
    comprobar(resumen_texto.find("Árbitros (agregado") == std::string::npos,
              "el resumen no incluye la tabla de árbitros");
    comprobar(resumen_texto.find("#7") == std::string::npos,
              "el resumen no lista ids de árbitros");

    std::ostringstream json;
    referee_scheduler::io::escribir_informe_json(json, resumen, "ejemplo.json");
    const auto raiz = parsear_json(json.str());

    comprobar(raiz.es_objeto(), "el informe es un objeto JSON");
    comprobar(raiz.buscar("coste_total_centimos") != nullptr &&
                  raiz.buscar("coste_total_centimos")->numero() == 29.0,
              "coste total en el JSON");
    comprobar(raiz.buscar("arbitros_globales") != nullptr &&
                  raiz.buscar("arbitros_globales")->array().size() == 1,
              "un árbitro global en el JSON");

    const auto *jornadas = raiz.buscar("jornadas");
    comprobar(jornadas != nullptr && jornadas->array().size() == 1,
              "una jornada en el JSON");

    const auto &jornada = jornadas->array().front();
    comprobar(jornada.buscar("fecha") != nullptr &&
                  jornada.buscar("fecha")->texto() == "2026-11-20",
              "fecha de la jornada");
    comprobar(jornada.buscar("causa") != nullptr &&
                  jornada.buscar("causa")->texto() == "optima",
              "causa de la jornada");

    const auto *asignaciones = jornada.buscar("asignaciones");
    comprobar(asignaciones != nullptr && asignaciones->array().size() == 1,
              "una asignación en el JSON");
    const auto &asignacion = asignaciones->array().front();
    comprobar(asignacion.buscar("arbitro_id") != nullptr &&
                  asignacion.buscar("arbitro_id")->numero() == 7.0,
              "árbitro de la asignación");
    comprobar(asignacion.buscar("cubierta") != nullptr &&
                  asignacion.buscar("cubierta")->booleano(),
              "asignación cubierta");

    const auto *arbitros = jornada.buscar("arbitros");
    comprobar(arbitros != nullptr && arbitros->array().size() == 1,
              "métricas del árbitro en el JSON");
    comprobar(arbitros->array().front().buscar("minutos_viaje")->numero() == 2.0,
              "minutos de viaje del árbitro en el JSON");

    if (fallos != 0)
    {
        std::cerr << fallos << " fallo(s)\n";
        return 1;
    }

    std::cout << "test_informe: OK\n";
    return 0;
}
