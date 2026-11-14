// Implementación de las fases 1 y 2 de los datos estáticos.
//
// - Fase 1 (`agrupar_por_dia`): fecha civil de cada partido calculada una sola
//   vez, orden por día y, a igualdad de día, por posición de entrada para que el
//   resultado sea determinista; los partidos de un día quedan contiguos.
// - Fase 2 (`compactar_arbitros`): selecciona los árbitros que pueden empezar
//   algún partido del día y les asigna ids `0..N_dia-1`.

#include "referee_scheduler/algorithms/construir_instancia.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace referee_scheduler::algorithms
{

    std::vector<GrupoDia> agrupar_por_dia(const domain::Instancia &instancia)
    {
        // Se calcula una sola vez la fecha de cada partido. `dia_de` consulta la
        // zona horaria local y no conviene repetirlo dentro del comparador de
        // una ordenación (se llamaría O(n log n) veces).
        struct Entrada
        {
            std::int64_t indice;   // días desde 1970-01-01
            std::size_t posicion;  // posición en Instancia::partidos
            DiaCivil dia;
        };

        std::vector<Entrada> entradas;
        entradas.reserve(instancia.partidos.size());
        for (std::size_t i = 0; i < instancia.partidos.size(); ++i)
        {
            const DiaCivil dia = dia_de(instancia.partidos[i].inicio);
            entradas.push_back(Entrada{indice_dia(dia), i, dia});
        }

        // Orden por fecha y, a igualdad de fecha, por posición de entrada. El
        // segundo criterio hace el resultado determinista (no depende del
        // algoritmo de ordenación ni del orden del montón).
        std::sort(entradas.begin(), entradas.end(),
                  [](const Entrada &izquierda, const Entrada &derecha)
                  {
                      if (izquierda.indice != derecha.indice)
                      {
                          return izquierda.indice < derecha.indice;
                      }
                      return izquierda.posicion < derecha.posicion;
                  });

        // Recorrido lineal creando un grupo cada vez que cambia el día. Como la
        // lista está ordenada, todos los partidos del mismo día son contiguos.
        std::vector<GrupoDia> grupos;
        for (const Entrada &entrada : entradas)
        {
            if (grupos.empty() || grupos.back().dia != entrada.dia)
            {
                grupos.push_back(GrupoDia{entrada.dia, {}});
            }
            grupos.back().partidos.push_back(entrada.posicion);
        }

        return grupos;
    }

    std::vector<ArbitroDia> compactar_arbitros(const domain::Instancia &instancia,
                                               const GrupoDia &grupo)
    {
        // El día como intervalo absoluto de minutos: [inicio_dia, fin_dia).
        const std::int64_t inicio_dia = indice_dia(grupo.dia) * kMinutosPorDia;
        const std::int64_t fin_dia = inicio_dia + kMinutosPorDia;

        // Equipos que juegan ese día. Los vetos contra equipos que no aparecen
        // en ninguna plaza no pueden afectar a ninguna decisión, así que se
        // descartan ya para que el filtro de candidatos recorra menos datos.
        std::vector<int> equipos_del_dia;
        equipos_del_dia.reserve(2 * grupo.partidos.size());
        for (const std::size_t posicion : grupo.partidos)
        {
            const domain::Partido &partido = instancia.partidos[posicion];
            equipos_del_dia.push_back(partido.equipo_local_id);
            equipos_del_dia.push_back(partido.equipo_visitante_id);
        }
        std::sort(equipos_del_dia.begin(), equipos_del_dia.end());
        equipos_del_dia.erase(
            std::unique(equipos_del_dia.begin(), equipos_del_dia.end()),
            equipos_del_dia.end());

        std::vector<ArbitroDia> compactados;
        compactados.reserve(instancia.arbitros.size());

        for (const domain::Arbitro &arbitro : instancia.arbitros)
        {
            // Ventana de disponibilidad en minutos absolutos. Comparar en
            // absoluto (y no los minutos ya recortados al día) es lo que evita
            // incluir a un árbitro cuya disponibilidad fue solo el día anterior:
            // el recorte convertiría su ventana en [0, 0], que sí parece tocar
            // el día actual.
            const std::int64_t disponible_desde =
                minuto_absoluto(arbitro.disponibilidad_inicio);
            const std::int64_t disponible_hasta =
                minuto_absoluto(arbitro.disponibilidad_fin);

            // Una ventana invertida o vacía es un dato inválido: no puede
            // empezar ningún partido y no entra en la jornada.
            if (disponible_hasta < disponible_desde)
            {
                continue;
            }

            // Los inicios posibles del día son [inicio_dia, fin_dia - 1]. Basta
            // con que la ventana toque alguno: si acaba justo a las 23:00, un
            // partido que empieza a esa hora sigue siendo válido.
            if (disponible_hasta < inicio_dia || disponible_desde >= fin_dia)
            {
                continue;
            }

            ArbitroDia compactado;
            compactado.id_global = arbitro.id;
            compactado.nivel = arbitro.nivel;
            compactado.sede_domicilio_global = arbitro.sede_id;

            // Recorte de la ventana al día. `minuto_en_dia` devuelve 0 si
            // empezaba antes y 1440 si acaba después (o justo al terminar el
            // día), que es el valor "sin límite" para `ultimo_inicio`.
            compactado.primer_inicio =
                minuto_en_dia(arbitro.disponibilidad_inicio, grupo.dia);
            compactado.ultimo_inicio =
                minuto_en_dia(arbitro.disponibilidad_fin, grupo.dia);

            // Vetos: solo los equipos presentes ese día, sin duplicados y
            // ordenados para poder usar `std::binary_search` al filtrar plazas.
            for (const int equipo : arbitro.vetos)
            {
                if (std::binary_search(equipos_del_dia.begin(),
                                       equipos_del_dia.end(), equipo))
                {
                    compactado.equipos_vetados.push_back(equipo);
                }
            }
            std::sort(compactado.equipos_vetados.begin(),
                      compactado.equipos_vetados.end());
            compactado.equipos_vetados.erase(
                std::unique(compactado.equipos_vetados.begin(),
                            compactado.equipos_vetados.end()),
                compactado.equipos_vetados.end());

            compactados.push_back(std::move(compactado));
        }

        // Orden por id global: el id compacto (índice) queda determinado por los
        // datos y no por el orden de entrada del cargador.
        std::sort(compactados.begin(), compactados.end(),
                  [](const ArbitroDia &izquierda, const ArbitroDia &derecha)
                  {
                      return izquierda.id_global < derecha.id_global;
                  });

        return compactados;
    }

} // namespace referee_scheduler::algorithms
