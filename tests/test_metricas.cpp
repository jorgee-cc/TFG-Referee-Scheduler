// Pruebas de `calcular_metricas`: reconstrucción del recorrido de cada árbitro
// a partir de una `InstanciaDia` y una `SolucionDia` fijas.
//
// El escenario es un día con dos plazas para el mismo árbitro y una tercera
// vacía:
//   - sale del domicilio (10 min),
//   - cubre el partido A (18:00-19:00),
//   - viaja a la sede B (5 min) y espera 55 min,
//   - cubre el partido B (20:00-21:00),
//   - vuelve a casa (15 min).
// Total: 30 min de viaje, 55 de espera, 120 de partido y 205 min dedicados.
//
// Es el ejecutable `referee_scheduler_metricas_tests` de CTest.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "referee_scheduler/algorithms/metricas.hpp"

namespace
{

    using referee_scheduler::algorithms::ArbitroDia;
    using referee_scheduler::algorithms::DecisionDia;
    using referee_scheduler::algorithms::InstanciaDia;
    using referee_scheduler::algorithms::MetricasJornada;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::PlazaDia;
    using referee_scheduler::algorithms::SolucionDia;
    using referee_scheduler::algorithms::calcular_metricas;
    using referee_scheduler::domain::Rol;

    int fallos = 0;

    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

    void comprobar_aprox(double valor, double esperado, const char *mensaje)
    {
        if (std::fabs(valor - esperado) > 1e-6)
        {
            std::cerr << "FALLO: " << mensaje << " (" << valor << " != "
                      << esperado << ")\n";
            ++fallos;
        }
    }

    PlazaDia plaza(int id, int partido, Rol rol, int sede, int inicio, int fin,
                   std::int64_t retribucion)
    {
        PlazaDia p;
        p.minuto_inicio = inicio;
        p.minuto_fin = fin;
        p.sede = static_cast<std::uint16_t>(sede);
        p.nivel_requerido = 1;
        p.retribucion_centimos = retribucion;
        p.rol = rol;
        p.partido_id = partido;
        p.plaza_id = id;
        p.equipo_local_id = 1;
        p.equipo_visitante_id = 2;
        return p;
    }

    InstanciaDia construir_dia()
    {
        InstanciaDia dia;

        // Índices compactos: 0 = sede A, 1 = sede B, 2 = domicilio.
        dia.sede_global = {10, 11, 90};
        dia.distancias = {
            0, 5, 10,
            5, 0, 15,
            10, 20, 0,
        };

        ArbitroDia primero;
        primero.id_global = 7;
        primero.nivel = 2;
        primero.domicilio_global = 90;
        primero.domicilio = 2;
        primero.primer_inicio = 0;
        primero.ultimo_inicio = 1440;
        dia.arbitros.push_back(primero);

        ArbitroDia segundo;
        segundo.id_global = 8;
        segundo.nivel = 1;
        segundo.domicilio_global = 90;
        segundo.domicilio = 2;
        segundo.primer_inicio = 0;
        segundo.ultimo_inicio = 1440;
        dia.arbitros.push_back(segundo);

        dia.plazas.push_back(plaza(1000, 100, Rol::Principal, 0, 1080, 1140,
                                   4500));
        dia.plazas.push_back(plaza(1001, 101, Rol::Asistente, 1, 1200, 1260,
                                   3000));
        dia.plazas.push_back(plaza(1002, 102, Rol::Anotador, 1, 1300, 1350,
                                   2000));

        return dia;
    }

    SolucionDia construir_solucion()
    {
        SolucionDia solucion;
        solucion.coste = 2982; // 982 de tiempo + 2000 de la plaza vacía.

        DecisionDia primera;
        primera.plaza_id = 1000;
        primera.partido_id = 100;
        primera.arbitro_global = 7;
        solucion.decisiones.push_back(primera);

        DecisionDia segunda;
        segunda.plaza_id = 1001;
        segunda.partido_id = 101;
        segunda.arbitro_global = 7;
        solucion.decisiones.push_back(segunda);

        DecisionDia vacia;
        vacia.plaza_id = 1002;
        vacia.partido_id = 102;
        vacia.arbitro_global = -1;
        solucion.decisiones.push_back(vacia);

        return solucion;
    }

} // namespace

int main()
{
    const InstanciaDia dia = construir_dia();
    const SolucionDia solucion = construir_solucion();
    const MetricasJornada metricas = calcular_metricas(dia, solucion);

    comprobar(metricas.plazas_totales == 3, "plazas totales");
    comprobar(metricas.plazas_cubiertas == 2, "plazas cubiertas");
    comprobar(metricas.arbitros_disponibles == 2, "árbitros disponibles");
    comprobar(metricas.arbitros_usados == 1, "árbitros usados");
    comprobar(metricas.arbitros.size() == 1, "un árbitro con plazas");

    comprobar(metricas.retribucion_cubierta_centimos == 7500,
              "retribución cubierta");
    comprobar(metricas.retribucion_perdida_centimos == 2000,
              "retribución perdida");
    comprobar(metricas.coste_tiempo_centimos == 982, "coste de tiempo");

    comprobar(metricas.minutos_viaje == 30, "minutos de viaje del día");
    comprobar(metricas.minutos_espera == 55, "minutos de espera del día");
    comprobar(metricas.minutos_partido == 120, "minutos de partido del día");
    comprobar(metricas.minutos_totales == 205, "minutos dedicados del día");

    const auto &arbitro = metricas.arbitros.front();
    comprobar(arbitro.arbitro_global == 7, "id del árbitro");
    comprobar(arbitro.nivel == 2, "nivel del árbitro");
    comprobar(arbitro.n_plazas == 2, "plazas del árbitro");
    comprobar(arbitro.retribucion_centimos == 7500, "retribución del árbitro");
    comprobar(arbitro.n_desplazamientos == 3, "desplazamientos del árbitro");
    comprobar(arbitro.minutos_viaje == 30, "viaje del árbitro");
    comprobar(arbitro.minutos_espera == 55, "espera del árbitro");
    comprobar(arbitro.minutos_partido == 120, "partido del árbitro");
    comprobar(arbitro.minutos_totales == 205, "tiempo total del árbitro");
    comprobar_aprox(arbitro.euros_por_hora, 75.0 / (205.0 / 60.0),
                    "tarifa del árbitro");
    comprobar_aprox(metricas.euros_por_hora, 75.0 / (205.0 / 60.0),
                    "tarifa del día");

    if (fallos != 0)
    {
        std::cerr << fallos << " fallo(s)\n";
        return 1;
    }

    std::cout << "test_metricas: OK\n";
    return 0;
}
