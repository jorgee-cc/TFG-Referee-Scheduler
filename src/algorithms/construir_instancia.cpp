// Implementación de las fases 1-3 de los datos estáticos.
//
// - Fase 1 (`agrupar_por_dia`): fecha civil de cada partido calculada una sola
//   vez, orden por día y, a igualdad de día, por posición de entrada para que el
//   resultado sea determinista; los partidos de un día quedan contiguos.
// - Fase 2 (`compactar_arbitros`): selecciona los árbitros que pueden empezar
//   algún partido del día y les asigna ids `0..N_dia-1`.
// - Fase 3 (`construir_matriz_distancias`): compacta las sedes implicadas,
//   pide la matriz de minutos al proveedor, fuerza la diagonal a 0 y aplica el
//   cierre métrico si se pide.

#include "referee_scheduler/algorithms/construir_instancia.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <unordered_map>
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

    MatrizDistancias construir_matriz_distancias(
        const domain::Instancia &instancia,
        const GrupoDia &grupo,
        std::vector<ArbitroDia> &arbitros,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones)
    {
        // 1. Sedes implicadas: las de los partidos del día y los domicilios de
        // sus árbitros. Un domicilio puede no albergar ningún partido, pero sin
        // su distancia no se puede calcular ni el primer desplazamiento ni el
        // viaje de vuelta.
        std::vector<int> ids_sede;
        ids_sede.reserve(grupo.partidos.size() + arbitros.size());
        for (const std::size_t posicion : grupo.partidos)
        {
            ids_sede.push_back(instancia.partidos[posicion].sede_id);
        }
        for (const ArbitroDia &arbitro : arbitros)
        {
            ids_sede.push_back(arbitro.sede_domicilio_global);
        }

        // Orden ascendente y sin duplicados: el índice compacto depende solo de
        // los datos, no del orden en que aparezcan partidos y árbitros.
        std::sort(ids_sede.begin(), ids_sede.end());
        ids_sede.erase(std::unique(ids_sede.begin(), ids_sede.end()),
                       ids_sede.end());

        // `EstadoArbitro::sede_id` tiene 10 bits: como mucho 1024 sedes.
        assert(ids_sede.size() <= 1024 &&
               "demasiadas sedes en un dia para los 10 bits del estado");

        // 2. Índice de sedes del dominio por id (una búsqueda lineal por sede
        // sería O(M * S); con el mapa se resuelve en una pasada).
        std::unordered_map<int, const domain::Sede *> sedes_por_id;
        sedes_por_id.reserve(instancia.sedes.size());
        for (const domain::Sede &sede : instancia.sedes)
        {
            sedes_por_id.emplace(sede.id, &sede);
        }

        std::unordered_map<int, std::uint16_t> indice_compacto;
        indice_compacto.reserve(ids_sede.size());

        std::vector<Coordenada> coordenadas;
        coordenadas.reserve(ids_sede.size());
        for (std::size_t i = 0; i < ids_sede.size(); ++i)
        {
            const auto encontrada = sedes_por_id.find(ids_sede[i]);
            assert(encontrada != sedes_por_id.end() &&
                   "un partido o un arbitro referencia una sede inexistente");

            const domain::Sede &sede = *encontrada->second;

            // (0, 0) es el golfo de Guinea: en este dominio significa "sede sin
            // geolocalizar" y haría que OSRM devolviera rutas absurdas.
            assert(!(sede.latitud == 0.0 && sede.longitud == 0.0) &&
                   "sede sin coordenadas: no se puede pedir la matriz");

            coordenadas.push_back(Coordenada{sede.latitud, sede.longitud});
            indice_compacto.emplace(ids_sede[i],
                                    static_cast<std::uint16_t>(i));
        }

        // 3. Matriz completa: el proveedor la devuelve en el mismo orden que las
        // coordenadas. No se espeja porque el viaje real es asimétrico.
        std::vector<Minutos> valores = proveedor.matriz_minutos(coordenadas);
        const std::size_t n = ids_sede.size();
        assert(valores.size() == n * n &&
               "el proveedor debe devolver n*n minutos");

        // La distancia de una sede a sí misma es 0 por definición: se impone en
        // vez de confiar en que el proveedor la traiga bien.
        for (std::size_t i = 0; i < n; ++i)
        {
            valores[i * n + i] = 0;
        }

        if (opciones.cerrar_metricamente)
        {
            cerrar_metricamente(valores, n);
        }

        // 4. El domicilio de cada árbitro pasa a ser un índice de la matriz.
        for (ArbitroDia &arbitro : arbitros)
        {
            arbitro.sede_domicilio = indice_compacto.at(
                arbitro.sede_domicilio_global);
        }

        return MatrizDistancias{std::move(ids_sede), std::move(valores)};
    }

    void cerrar_metricamente(std::vector<Minutos> &distancias,
                             std::size_t n_sedes)
    {
        assert(distancias.size() == n_sedes * n_sedes &&
               "la matriz debe ser cuadrada");

        for (std::size_t k = 0; k < n_sedes; ++k)
        {
            for (std::size_t i = 0; i < n_sedes; ++i)
            {
                const Minutos de_i_a_k = distancias[i * n_sedes + k];
                if (de_i_a_k == kMinutosInalcanzable)
                {
                    // Sin ruta hasta k no se puede usar k como puente.
                    continue;
                }

                for (std::size_t j = 0; j < n_sedes; ++j)
                {
                    const Minutos de_k_a_j = distancias[k * n_sedes + j];
                    if (de_k_a_j == kMinutosInalcanzable)
                    {
                        continue;
                    }

                    // Suma en 32 bits: si el alternativo mejora la celda es
                    // menor que ella (<= 65534), así que cabe en `Minutos`.
                    const std::uint32_t alternativo =
                        static_cast<std::uint32_t>(de_i_a_k) + de_k_a_j;
                    if (alternativo < distancias[i * n_sedes + j])
                    {
                        distancias[i * n_sedes + j] =
                            static_cast<Minutos>(alternativo);
                    }
                }
            }
        }
    }

    InstanciaDia construir_instancia_dia(
        const domain::Instancia &instancia,
        const GrupoDia &grupo,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones)
    {
        InstanciaDia dia;
        dia.dia = grupo.dia;

        // Fase 2: árbitros del día compactados a 0..N-1.
        dia.arbitros = compactar_arbitros(instancia, grupo);

        // Fase 3: sedes compactadas, matriz de tiempos y domicilio de cada
        // árbitro ya traducido a índice.
        MatrizDistancias matriz = construir_matriz_distancias(
            instancia, grupo, dia.arbitros, proveedor, opciones);
        dia.sede_global = std::move(matriz.sede_global);
        dia.distancias = std::move(matriz.valores);

        return dia;
    }

    std::vector<InstanciaDia> construir_instancias(
        const domain::Instancia &instancia,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones)
    {
        const std::vector<GrupoDia> grupos = agrupar_por_dia(instancia);

        std::vector<InstanciaDia> dias;
        dias.reserve(grupos.size());
        for (const GrupoDia &grupo : grupos)
        {
            dias.push_back(
                construir_instancia_dia(instancia, grupo, proveedor, opciones));
        }

        return dias;
    }

} // namespace referee_scheduler::algorithms
