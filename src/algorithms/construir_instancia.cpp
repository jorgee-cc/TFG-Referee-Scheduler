// Implementación de las fases 1-5 de los datos estáticos.
//
// - Fase 1 (`agrupar_por_dia`): fecha civil de cada partido calculada una sola
//   vez, orden por día y, a igualdad de día, por posición de entrada para que el
//   resultado sea determinista; los partidos de un día quedan contiguos.
// - Fase 2 (`compactar_arbitros`): selecciona los árbitros que pueden empezar
//   algún partido del día y les asigna ids `0..N_dia-1`.
// - Fase 3 (`construir_matriz_distancias`): compacta las ubicaciones implicadas,
//   pide la matriz de minutos al proveedor, fuerza la diagonal a 0, eleva a 1
//   minuto los desplazamientos entre puntos distintos que OSRM devuelve como 0
//   y aplica el cierre métrico si se pide.
// - Fase 4 (`construir_plazas`): expande los partidos del día en plazas con
//   horario, sede compacta, nivel, retribución en céntimos y equipos, ordenadas
//   por `(minuto_inicio, sede, partido_id, plaza_id)` para que el índice sea el
//   nivel del árbol.
// - Fase 5 (`construir_candidatos`): para cada plaza deja en un CSR los ids
//   compactos de los árbitros que cumplen nivel, ventana, vetos y tienen ruta
//   desde su domicilio. No decide la factibilidad dinámica: eso depende del
//   estado y lo comprueba el generador de hijos.

#include "referee_scheduler/algorithms/construir_instancia.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

#include "referee_scheduler/algorithms/objetivo.hpp"

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
            compactado.domicilio_global = arbitro.domicilio_id;

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
        // 1. Ubicaciones implicadas: las sedes de los partidos del día y los
        // domicilios de sus árbitros. Un domicilio no alberga ningún partido,
        // pero sin su distancia no se puede calcular ni el primer
        // desplazamiento ni el viaje de vuelta.
        std::vector<int> ids_sede;
        ids_sede.reserve(grupo.partidos.size() + arbitros.size());
        for (const std::size_t posicion : grupo.partidos)
        {
            ids_sede.push_back(instancia.partidos[posicion].sede_id);
        }
        for (const ArbitroDia &arbitro : arbitros)
        {
            ids_sede.push_back(arbitro.domicilio_global);
        }

        // Disyunción domicilios / sedes de partido: un domicilio no puede ser
        // la sede de un partido. Si coincidieran, el estado inicial de ese
        // árbitro (sede = domicilio) sería indistinguible del de un árbitro que
        // acaba de arbitrar en esa sede, y la función objetivo no sabría si
        // puede salir de casa antes de su disponibilidad. Se comprueba contra
        // todos los partidos de la instancia, no solo los del día, porque es un
        // contrato del cargador y no una casualidad de esta jornada.
        {
            std::vector<int> sedes_partido;
            sedes_partido.reserve(instancia.partidos.size());
            for (const domain::Partido &partido : instancia.partidos)
            {
                sedes_partido.push_back(partido.sede_id);
            }
            std::sort(sedes_partido.begin(), sedes_partido.end());

            for (const ArbitroDia &arbitro [[maybe_unused]] : arbitros)
            {
                assert(!std::binary_search(sedes_partido.begin(),
                                           sedes_partido.end(),
                                           arbitro.domicilio_global) &&
                       "un domicilio de arbitro coincide con una sede de partido");
            }
        }

        // Orden ascendente y sin duplicados: el índice compacto depende solo de
        // los datos, no del orden en que aparezcan partidos y árbitros.
        std::sort(ids_sede.begin(), ids_sede.end());
        ids_sede.erase(std::unique(ids_sede.begin(), ids_sede.end()),
                       ids_sede.end());

        // `EstadoArbitro::sede_id` tiene 10 bits: como mucho 1024 ubicaciones.
        assert(ids_sede.size() <= 1024 &&
               "demasiadas ubicaciones en un dia para los 10 bits del estado");

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

        // La distancia de una ubicación a sí misma es 0 por definición: se
        // impone en vez de confiar en que el proveedor la traiga bien.
        //
        // Cualquier otro desplazamiento cuesta al menos `kMinutosViajeMinimo`:
        // OSRM devuelve 0 minutos entre dos puntos muy cercanos (un domicilio y
        // la sede del partido en la misma calle) y un desplazamiento nunca es
        // gratis. Elevar los ceros no rompe la desigualdad triangular: un
        // camino entre dos ubicaciones distintas tiene al menos dos aristas, y
        // cada una valdrá también 1 minuto o más.
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = 0; j < n; ++j)
            {
                Minutos &celda = valores[i * n + j];
                if (i == j)
                {
                    celda = 0;
                }
                else if (celda == 0)
                {
                    celda = kMinutosViajeMinimo;
                }
            }
        }

        if (opciones.cerrar_metricamente)
        {
            cerrar_metricamente(valores, n);
        }

        // 4. El domicilio de cada árbitro pasa a ser un índice de la matriz.
        for (ArbitroDia &arbitro : arbitros)
        {
            arbitro.domicilio = indice_compacto.at(arbitro.domicilio_global);
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

    std::vector<PlazaDia> construir_plazas(const domain::Instancia &instancia,
                                           const GrupoDia &grupo,
                                           const std::vector<int> &sede_global)
    {
        /// Datos del partido que necesita cada una de sus plazas.
        ///
        /// Se calculan una sola vez por partido (y no una vez por plaza) porque
        /// `minuto_absoluto` descompone el `time_t` con la zona horaria local.
        struct PartidoDia
        {
            const domain::Partido *partido = nullptr;
            int minuto_inicio = 0;
            int minuto_fin = 0;
            std::uint16_t sede = 0;
        };

        // Las 00:00 del día de la jornada: todos los minutos del registro son
        // relativos a este instante.
        const std::int64_t base_dia = indice_dia(grupo.dia) * kMinutosPorDia;

        // 1. Datos por partido (minutos, sede compacta). Los partidos no se
        // copian: se apunta a los de la instancia de entrada, que vive más que
        // este vector.
        std::unordered_map<int, PartidoDia> partidos_del_dia;
        partidos_del_dia.reserve(grupo.partidos.size());

        for (const std::size_t posicion : grupo.partidos)
        {
            const domain::Partido &partido = instancia.partidos[posicion];

            PartidoDia datos;
            datos.partido = &partido;

            // El grupo se construyó con la fecha civil de `inicio`, así que el
            // comienzo cae siempre dentro del día. `final`, en cambio, puede
            // caer de madrugada del día siguiente: se mide en absoluto contra la
            // base del día para conservar la hora real (1500 y no 60).
            datos.minuto_inicio = static_cast<int>(
                minuto_absoluto(partido.inicio) - base_dia);
            datos.minuto_fin = static_cast<int>(
                minuto_absoluto(partido.final) - base_dia);

            assert(datos.minuto_inicio >= 0 &&
                   datos.minuto_inicio < kMinutosPorDia &&
                   "un partido del dia debe empezar dentro del dia");
            assert(datos.minuto_fin >= datos.minuto_inicio &&
                   "un partido no puede acabar antes de empezar");
            assert(datos.minuto_fin < 2048 &&
                   "el final del partido no cabe en los 11 bits del estado");

            // Sede compacta. `sede_global` está ordenado (fase 3), así que la
            // búsqueda binaria devuelve el índice de la matriz en O(log M).
            const auto encontrada = std::lower_bound(
                sede_global.begin(), sede_global.end(), partido.sede_id);
            assert(encontrada != sede_global.end() &&
                   *encontrada == partido.sede_id &&
                   "la sede del partido no esta en la matriz del dia");
            datos.sede = static_cast<std::uint16_t>(
                std::distance(sede_global.begin(), encontrada));

            partidos_del_dia.emplace(partido.id, datos);
        }

        // 2. Expansión: una plaza por cada `domain::Plaza` de un partido del día.
        std::vector<PlazaDia> plazas;
        plazas.reserve(instancia.plazas.size());

        for (const domain::Plaza &plaza : instancia.plazas)
        {
            const auto encontrado = partidos_del_dia.find(plaza.partido_id);
            if (encontrado == partidos_del_dia.end())
            {
                // La plaza es de un partido de otra jornada: no entra hoy.
                continue;
            }

            const PartidoDia &datos = encontrado->second;
            const domain::Partido &partido = *datos.partido;

            PlazaDia expandida;
            expandida.minuto_inicio = datos.minuto_inicio;
            expandida.minuto_fin = datos.minuto_fin;
            expandida.sede = datos.sede;
            expandida.nivel_requerido = plaza.nivel_requerido;

            // Conversión a céntimos una sola vez: el coste de dejar vacía la
            // plaza y todas las comparaciones trabajan en enteros.
            expandida.retribucion_centimos = a_centimos(plaza.retribucion);

            expandida.rol = plaza.rol;
            expandida.partido_id = partido.id;
            expandida.plaza_id = plaza.id;
            expandida.equipo_local_id = partido.equipo_local_id;
            expandida.equipo_visitante_id = partido.equipo_visitante_id;

            assert(expandida.nivel_requerido >= 0 &&
                   "el nivel requerido no puede ser negativo");
            assert(expandida.retribucion_centimos >= 0 &&
                   "la retribucion no puede ser negativa");

            plazas.push_back(expandida);
        }

        // 3. Orden total `(minuto_inicio, sede, partido_id, plaza_id)`. Los ids
        // desempatan siempre, así que el índice de cada plaza es estable y se
        // puede usar como nivel del árbol.
        std::sort(plazas.begin(), plazas.end(),
                  [](const PlazaDia &izquierda, const PlazaDia &derecha)
                  {
                      if (izquierda.minuto_inicio != derecha.minuto_inicio)
                      {
                          return izquierda.minuto_inicio < derecha.minuto_inicio;
                      }
                      if (izquierda.sede != derecha.sede)
                      {
                          return izquierda.sede < derecha.sede;
                      }
                      if (izquierda.partido_id != derecha.partido_id)
                      {
                          return izquierda.partido_id < derecha.partido_id;
                      }
                      return izquierda.plaza_id < derecha.plaza_id;
                  });

        return plazas;
    }

    ListasCandidatos construir_candidatos(const InstanciaDia &dia)
    {
        const std::size_t n_arbitros = dia.n_arbitros();

        // `viaje` indexa la matriz directamente: si la fase 3 no terminó, leería
        // fuera de rango. Se comprueba el tamaño completo de una vez.
        assert(dia.distancias.size() == dia.n_sedes() * dia.n_sedes() &&
               "la matriz de distancias no esta completa");
        // Los ids del CSR son de 32 bits. Con `kMaxArbitros = 600` sobra, pero el
        // contrato queda escrito por si la capacidad cambiara.
        assert(n_arbitros <= std::numeric_limits<std::uint32_t>::max() &&
               "no caben tantos arbitros en un indice de 32 bits");

        ListasCandidatos listas;

        // La frontera 0 y una por plaza: la ultima se rellena al cerrar la
        // ultima plaza, asi que al final hay `n_plazas + 1`.
        listas.offsets.reserve(dia.n_plazas() + 1);
        listas.offsets.push_back(0);

        // No se reserva `candidatos`: el numero de candidatos por plaza no se
        // conoce sin hacer el filtrado y el crecimiento amortizado de `push_back`
        // es despreciable para unos miles de enteros.

        // Un veto es una búsqueda binaria: la fase 2 dejó `equipos_vetados`
        // ordenado y limitado a los equipos que juegan ese día.
        const auto vetado = [](const ArbitroDia &arbitro, int equipo)
        {
            return std::binary_search(arbitro.equipos_vetados.begin(),
                                      arbitro.equipos_vetados.end(), equipo);
        };

        for (const PlazaDia &plaza : dia.plazas)
        {
            // Se recorre `arbitros` en orden, asi que los ids de cada plaza
            // salen ascendentes: el desempate es siempre el mismo.
            for (std::size_t id = 0; id < n_arbitros; ++id)
            {
                const ArbitroDia &arbitro = dia.arbitros[id];

                // 1. Nivel: una plaza exige un minimo y el nivel no baja al
                //    cubrir partidos, asi que el descarte es definitivo.
                if (arbitro.nivel < plaza.nivel_requerido)
                {
                    continue;
                }

                // 2. Ventana: la disponibilidad limita la hora de INICIO del
                //    partido, no su final. No se suma el viaje desde el
                //    domicilio: el primer desplazamiento puede empezar antes de
                //    `primer_inicio` (se sale de casa en `inicio - viaje`).
                if (plaza.minuto_inicio < arbitro.primer_inicio ||
                    plaza.minuto_inicio > arbitro.ultimo_inicio)
                {
                    continue;
                }

                // 3. Vetos: basta con tener vetado a uno de los dos equipos.
                if (vetado(arbitro, plaza.equipo_local_id) ||
                    vetado(arbitro, plaza.equipo_visitante_id))
                {
                    continue;
                }

                // 4. Ruta: sin camino desde el domicilio no hay forma de cubrir
                //    la plaza ni siquiera como primer partido. Cuanto tarde el
                //    viaje ya no importa aqui (ver el comentario de la ventana).
                if (dia.viaje(arbitro.domicilio, plaza.sede) ==
                    kMinutosInalcanzable)
                {
                    continue;
                }

                listas.candidatos.push_back(static_cast<std::uint32_t>(id));
            }

            // Frontera de la plaza recien filtrada: todos los candidatos
            // anadidos hasta ahora, incluidos los de las plazas anteriores.
            listas.offsets.push_back(
                static_cast<std::uint32_t>(listas.candidatos.size()));
        }

        assert(listas.offsets.size() == dia.n_plazas() + 1 &&
               "el CSR debe tener una frontera por plaza mas la inicial");
        assert(listas.offsets.back() == listas.candidatos.size() &&
               "la ultima frontera debe cerrar el CSR");

        return listas;
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

        // Fase 4: plazas del día ordenadas cronológicamente. Depende de la fase
        // 3 para traducir la sede del partido a índice compacto.
        dia.plazas = construir_plazas(instancia, grupo, dia.sede_global);

        // Fase 5: candidatos de cada plaza. Necesita la fase 2 (nivel, ventana y
        // vetos), la fase 3 (domicilio compacto y existencia de ruta) y la fase 4
        // (plazas ya ordenadas: el CSR sigue ese mismo orden, plaza a plaza).
        ListasCandidatos listas = construir_candidatos(dia);
        dia.candidatos = std::move(listas.candidatos);
        dia.offset_candidatos = std::move(listas.offsets);

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
