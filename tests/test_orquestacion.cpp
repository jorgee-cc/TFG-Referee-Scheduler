// Pruebas de la orquestación completa sin red: instancia -> fases 1-5 ->
// Dijkstra -> métricas -> resumen.
//
// Se usa un `ProveedorDistancias` falso que devuelve 1 minuto entre cualquier
// pareja de ubicaciones (la diagonal la fuerza a 0 la fase 3). Así los costes
// son fáciles de calcular a mano y el test no depende de OSRM.
//
// Cubre:
//  - día normal con un árbitro y una plaza principal;
//  - día sin árbitros y solo con plazas secundarias (todas vacías);
//  - día con una principal sin candidatos (sin solución);
//  - integración con el cargador JSON.
//
// Es el ejecutable `referee_scheduler_orquestacion_tests` de CTest.

#include <cstddef>
#include <ctime>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "referee_scheduler/algorithms/orquestacion.hpp"
#include "referee_scheduler/algorithms/proveedor_distancias.hpp"
#include "referee_scheduler/domain/instancia.hpp"
#include "referee_scheduler/io/instancia_json.hpp"

namespace
{

    using referee_scheduler::algorithms::CausaParada;
    using referee_scheduler::algorithms::Coordenada;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::ProveedorDistancias;
    using referee_scheduler::algorithms::ResumenEjecucion;
    using referee_scheduler::algorithms::resolver_instancia;
    using referee_scheduler::domain::Arbitro;
    using referee_scheduler::domain::Equipo;
    using referee_scheduler::domain::Instancia;
    using referee_scheduler::domain::Partido;
    using referee_scheduler::domain::Plaza;
    using referee_scheduler::domain::Rol;
    using referee_scheduler::domain::Sede;

    int fallos = 0;

    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

    std::time_t instante(int year, int mes, int dia, int hora, int minuto)
    {
        std::tm partes{};
        partes.tm_year = year - 1900;
        partes.tm_mon = mes - 1;
        partes.tm_mday = dia;
        partes.tm_hour = hora;
        partes.tm_min = minuto;
        partes.tm_isdst = -1;
        return std::mktime(&partes);
    }

    /// Proveedor de prueba: todo cuesta 1 minuto. La fase 3 pone la diagonal a
    /// 0, así que la matriz resultante es la identidad con 1 fuera.
    class ProveedorFijo final : public ProveedorDistancias
    {
    public:
        std::vector<Minutos>
        matriz_minutos(std::span<const Coordenada> coordenadas) override
        {
            return std::vector<Minutos>(coordenadas.size() * coordenadas.size(),
                                        1);
        }
    };

    Sede sede(int id, const char *nombre, double latitud, double longitud)
    {
        Sede s;
        s.id = id;
        s.nombre = nombre;
        s.latitud = latitud;
        s.longitud = longitud;
        return s;
    }

    /// Instancia de un día con un partido en la sede 10 y un árbitro (opcional)
    /// que vive en la sede 90.
    Instancia instancia_basica(bool con_arbitro, bool con_principal,
                               int nivel_arbitro = 2)
    {
        Instancia instancia;

        Equipo local;
        local.id = 1;
        local.nombre = "Local";
        instancia.equipos.push_back(local);

        Equipo visitante;
        visitante.id = 2;
        visitante.nombre = "Visitante";
        instancia.equipos.push_back(visitante);

        instancia.sedes.push_back(sede(10, "Campo", 43.36, -8.41));
        instancia.sedes.push_back(sede(90, "Casa", 43.37, -8.40));

        if (con_arbitro)
        {
            Arbitro arbitro;
            arbitro.id = 7;
            arbitro.nivel = nivel_arbitro;
            arbitro.domicilio_id = 90;
            arbitro.disponibilidad_inicio = instante(2026, 11, 20, 8, 0);
            arbitro.disponibilidad_fin = instante(2026, 11, 20, 23, 0);
            instancia.arbitros.push_back(arbitro);
        }

        Partido partido;
        partido.id = 100;
        partido.sede_id = 10;
        partido.inicio = instante(2026, 11, 20, 18, 0);
        partido.final = instante(2026, 11, 20, 19, 0);
        partido.equipo_local_id = 1;
        partido.equipo_visitante_id = 2;
        instancia.partidos.push_back(partido);

        Plaza plaza;
        plaza.id = 1000;
        plaza.partido_id = 100;
        plaza.rol = con_principal ? Rol::Principal : Rol::Asistente;
        plaza.nivel_requerido = 2;
        plaza.retribucion = con_principal ? 45.0 : 25.0;
        instancia.plazas.push_back(plaza);

        return instancia;
    }

    std::string json_basico()
    {
        return R"({
            "equipos": [
                {"id": 1, "nombre": "Local"},
                {"id": 2, "nombre": "Visitante"}
            ],
            "sedes": [
                {"id": 10, "nombre": "Campo", "latitud": 43.36, "longitud": -8.41},
                {"id": 90, "nombre": "Casa", "latitud": 43.37, "longitud": -8.40}
            ],
            "arbitros": [
                {"id": 7, "nivel": 2, "domicilio_id": 90,
                 "disponibilidad_inicio": "2026-11-20T08:00",
                 "disponibilidad_fin": "2026-11-20T23:00"}
            ],
            "partidos": [
                {"id": 100, "sede_id": 10,
                 "inicio": "2026-11-20T18:00", "final": "2026-11-20T19:00",
                 "equipo_local_id": 1, "equipo_visitante_id": 2}
            ],
            "plazas": [
                {"id": 1000, "partido_id": 100, "rol": "principal",
                 "nivel_requerido": 2, "retribucion": 45.0}
            ]
        })";
    }

    void probar_dia_normal()
    {
        const Instancia instancia = instancia_basica(true, true);
        ProveedorFijo proveedor;
        const ResumenEjecucion resumen =
            resolver_instancia(instancia, proveedor);

        comprobar(resumen.jornadas.size() == 1, "una jornada");
        comprobar(resumen.hay_solucion, "hay solución");
        comprobar(resumen.todas_optimas, "solución óptima");
        comprobar(resumen.coste_total_centimos == 29,
                  "coste: epsilon + ida + vuelta (1 + 14 + 14)");
        comprobar(resumen.plazas_totales == 1 && resumen.plazas_cubiertas == 1,
                  "plaza cubierta");
        comprobar(resumen.arbitros_usados == 1, "un árbitro usado");
        comprobar(resumen.arbitros_en_instancia == 1, "un árbitro en la instancia");
        comprobar(resumen.arbitros.size() == 1, "un árbitro con métricas");

        const auto &jornada = resumen.jornadas.front();
        comprobar(jornada.busqueda.causa == CausaParada::Optima,
                  "causa óptima");
        comprobar(jornada.asignaciones.size() == 1, "una asignación");
        comprobar(jornada.asignaciones.front().cubierta(),
                  "asignación cubierta");
        comprobar(jornada.asignaciones.front().arbitro_global == 7,
                  "árbitro asignado");
        comprobar(jornada.metricas.retribucion_cubierta_centimos == 4500,
                  "retribución cubierta");
        comprobar(jornada.metricas.minutos_viaje == 2, "ida y vuelta");
        comprobar(jornada.metricas.minutos_espera == 0, "sin espera");
        comprobar(jornada.metricas.minutos_partido == 60, "minutos de partido");
        comprobar(jornada.metricas.minutos_totales == 62,
                  "tiempo total del árbitro");

        const auto &arbitro = resumen.arbitros.front();
        comprobar(arbitro.arbitro_global == 7, "id del árbitro agregado");
        comprobar(arbitro.retribucion_centimos == 4500,
                  "retribución agregada");
        comprobar(arbitro.n_desplazamientos == 2,
                  "dos desplazamientos (ida y vuelta)");
    }

    void probar_dia_sin_arbitros()
    {
        const Instancia instancia = instancia_basica(false, false);
        ProveedorFijo proveedor;
        const ResumenEjecucion resumen =
            resolver_instancia(instancia, proveedor);

        comprobar(resumen.hay_solucion, "día sin árbitros resuelto");
        comprobar(resumen.coste_total_centimos == 2500,
                  "coste = retribución perdida");
        comprobar(resumen.jornadas.size() == 1, "una jornada");
        comprobar(resumen.jornadas.front().asignaciones.size() == 1,
                  "una asignación vacía");
        comprobar(!resumen.jornadas.front().asignaciones.front().cubierta(),
                  "la plaza queda vacía");
        comprobar(resumen.jornadas.front().metricas.coste_tiempo_centimos == 0,
                  "sin coste de tiempo");
        comprobar(resumen.jornadas.front().metricas.minutos_viaje == 0,
                  "sin viajes");
    }

    void probar_dia_sin_solucion()
    {
        const Instancia instancia = instancia_basica(true, true, 0);
        ProveedorFijo proveedor;
        const ResumenEjecucion resumen =
            resolver_instancia(instancia, proveedor);

        comprobar(!resumen.hay_solucion, "no hay solución");
        comprobar(!resumen.todas_optimas, "no es óptima");
        comprobar(resumen.coste_total_centimos == 0, "coste cero sin solución");
        comprobar(resumen.jornadas.size() == 1, "una jornada");
        comprobar(resumen.jornadas.front().busqueda.causa ==
                      CausaParada::SinSolucion,
                  "causa sin solución");
        comprobar(resumen.jornadas.front().asignaciones.empty(),
                  "sin asignaciones");
        comprobar(resumen.jornadas.front().metricas.plazas_totales == 1,
                  "la plaza se contabiliza");
    }

    void probar_multi_dia()
    {
        Instancia instancia = instancia_basica(true, true);
        // El mismo árbitro disponible el viernes y el sábado.
        instancia.arbitros.front().disponibilidad_fin =
            instante(2026, 11, 22, 23, 0);

        Partido segundo;
        segundo.id = 101;
        segundo.sede_id = 10;
        segundo.inicio = instante(2026, 11, 21, 18, 0);
        segundo.final = instante(2026, 11, 21, 19, 0);
        segundo.equipo_local_id = 1;
        segundo.equipo_visitante_id = 2;
        instancia.partidos.push_back(segundo);

        Plaza segunda;
        segunda.id = 1001;
        segunda.partido_id = 101;
        segunda.rol = Rol::Principal;
        segunda.nivel_requerido = 2;
        segunda.retribucion = 45.0;
        instancia.plazas.push_back(segunda);

        ProveedorFijo proveedor;
        const ResumenEjecucion resumen =
            resolver_instancia(instancia, proveedor);

        comprobar(resumen.jornadas.size() == 2, "dos jornadas");
        comprobar(resumen.hay_solucion, "multi-día con solución");
        comprobar(resumen.coste_total_centimos == 58,
                  "coste de las dos jornadas");
        comprobar(resumen.jornadas[0].dia ==
                      (referee_scheduler::algorithms::DiaCivil{2026, 11, 20}),
                  "primera fecha");
        comprobar(resumen.jornadas[1].dia ==
                      (referee_scheduler::algorithms::DiaCivil{2026, 11, 21}),
                  "segunda fecha");
        comprobar(resumen.arbitros.size() == 1,
                  "un árbitro agregado en multi-día");
        comprobar(resumen.arbitros.front().n_plazas == 2,
                  "dos plazas del mismo árbitro");
        comprobar(resumen.arbitros.front().retribucion_centimos == 9000,
                  "retribución agregada de las dos jornadas");
    }

    void probar_json()
    {
        const Instancia instancia =
            referee_scheduler::io::instancia_desde_json(json_basico());
        ProveedorFijo proveedor;
        const ResumenEjecucion resumen =
            resolver_instancia(instancia, proveedor);

        comprobar(resumen.hay_solucion, "JSON: hay solución");
        comprobar(resumen.coste_total_centimos == 29, "JSON: coste");
        comprobar(resumen.arbitros.size() == 1, "JSON: métricas del árbitro");
        comprobar(resumen.arbitros.front().arbitro_global == 7,
                  "JSON: id del árbitro");
        comprobar(resumen.jornadas.front().asignaciones.front().cubierta(),
                  "JSON: plaza cubierta");
    }

} // namespace

int main()
{
    probar_dia_normal();
    probar_dia_sin_arbitros();
    probar_dia_sin_solucion();
    probar_multi_dia();
    probar_json();

    if (fallos != 0)
    {
        std::cerr << fallos << " fallo(s)\n";
        return 1;
    }

    std::cout << "test_orquestacion: OK\n";
    return 0;
}
