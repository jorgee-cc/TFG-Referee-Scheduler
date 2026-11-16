// Implementación del motor de Dijkstra (paso 4/6).
//
// El bucle es deliberadamente corto: toda la lógica de coste está en
// `objetivo.hpp` y en el generador de hijos (`transiciones.hpp`), y toda la
// lógica de orden y de duplicados está en `ListaAbierta` y `TablaEstados`.

#include "referee_scheduler/algorithms/busqueda.hpp"

#include <cassert>
#include <chrono>
#include <limits>

namespace referee_scheduler::algorithms
{

    ResultadoBusqueda buscar_dijkstra(Arena &arena,
                                      const InstanciaDia &dia,
                                      const ParametrosObjetivo &parametros,
                                      const OpcionesBusqueda &opciones)
    {
        assert(arena.n_arbitros() == dia.n_arbitros() &&
               "la arena debe representar los arbitros del dia");
        assert(arena.nodos_creados() == 0 && "la arena de la busqueda debe estar vacia");
        assert(opciones.max_nodos > 0 && "el presupuesto de nodos debe ser positivo");
        assert(opciones.anchura_cubo > 0 && "la anchura de los cubos debe ser positiva");

        const auto inicio_reloj = std::chrono::steady_clock::now();

        ResultadoBusqueda resultado;

        // Frontera (cubos por céntimo) y memoria de estados ya vistos.
        ListaAbierta abierta(opciones.anchura_cubo);
        TablaEstados tabla;

        // Nodo 0: estado inicial (cada árbitro en su domicilio) con coste 0.
        const std::uint32_t raiz = crear_raiz(arena, dia);
        tabla.mejorar(arena, raiz);
        abierta.insertar(raiz, 0);

        // Mejor solución completa conocida hasta ahora (cota superior). Se usa
        // para la regla de parada y para no crear hijos que ya no pueden ganar.
        std::int64_t mejor = std::numeric_limits<std::int64_t>::max();
        std::uint32_t meta = kSinNodo;
        bool hay_meta = false;
        bool presupuesto_agotado = false;

        while (!abierta.vacia())
        {
            // Presupuesto: se comprueba una vez por expansión (la expansión
            // anterior pudo crear varios hijos y pasarse un poco del límite).
            if (arena.nodos_creados() >= opciones.max_nodos)
            {
                presupuesto_agotado = true;
                break;
            }

            // Regla de parada: cuando el mínimo de la frontera ya no puede
            // mejorar la meta conocida, no queda nada que explorar. Con cubos
            // exactos (`anchura_cubo == 1`) esto equivale a parar en la primera
            // meta extraída; con cubos agrupados hay que vaciar el cubo actual
            // antes de darla por óptima.
            if (hay_meta && abierta.clave_min() >= mejor)
            {
                break;
            }

            const std::uint32_t nodo = abierta.extraer_min();

            // El nodo fue sustituido por otro camino mejor para el mismo estado
            // mientras esperaba en la frontera: se descarta sin expandir.
            if ((arena.cabecera(nodo).flags & kFlagSuperado) != 0)
            {
                ++resultado.extracciones_obsoletas;
                continue;
            }

            // Cada nodo entra en la lista abierta como mucho una vez, así que no
            // puede expandirse dos veces (tampoco por una reapertura: con claves
            // no decrecientes y reemplazo solo si el coste es estrictamente
            // menor, un estado ya extraído tiene su coste definitivo).
            assert((arena.cabecera(nodo).flags & kFlagCerrado) == 0 &&
                   "un nodo no debe expandirse dos veces");
            arena.cabecera(nodo).flags |= kFlagCerrado;
            ++resultado.nodos_expandidos;

            const std::int64_t coste_nodo = arena.cabecera(nodo).coste_g;

            if (es_meta(arena, dia, nodo))
            {
                // Solución completa: su `coste_g` ya incluye viajes, esperas,
                // retornos y plazas vacías. Se guarda como incumbente; el bucle
                // seguirá solo si la frontera puede mejorarla.
                if (!hay_meta || coste_nodo < mejor)
                {
                    mejor = coste_nodo;
                    meta = nodo;
                    hay_meta = true;
                }
                continue;
            }

            // Expansión: el generador crea los hijos en la arena en orden
            // determinista (candidatos ascendentes y después la plaza vacía) y
            // cada uno pasa por la tabla antes de entrar en la frontera.
            generar_hijos(arena, dia, nodo, parametros, [&](std::uint32_t hijo)
            {
                const std::int64_t coste_hijo = arena.cabecera(hijo).coste_g;

                // Cota del incumbente: un hijo que ya cuesta lo mismo o más que
                // la mejor meta conocida no puede formar parte de una solución
                // mejor (las aristas no restan).
                if (hay_meta && coste_hijo >= mejor)
                {
                    ++resultado.hijos_descartados;
                    return;
                }

                // Consulta a la lista cerrada en el momento de generar: si el
                // estado (nivel + reparto) ya se vio con este coste o con uno
                // menor, el hijo no entra en la frontera y su subárbol no se
                // explora.
                if (!tabla.mejorar(arena, hijo))
                {
                    ++resultado.hijos_descartados;
                    return;
                }

                abierta.insertar(hijo, coste_hijo);
                if (abierta.size() > resultado.max_abierta)
                {
                    resultado.max_abierta = abierta.size();
                }
            });
        }

        // --- Cierre: reconstruir la solución y recoger métricas ---

        if (hay_meta)
        {
            resultado.hay_solucion = true;
            resultado.coste = mejor;
            resultado.meta = meta;
            resultado.solucion = reconstruir_solucion(arena, dia, meta);
            assert(resultado.solucion->coste == resultado.coste &&
                   "el coste reconstruido debe coincidir con el de la cabecera");
        }

        if (presupuesto_agotado)
        {
            resultado.causa = CausaParada::PresupuestoAgotado;
        }
        else if (hay_meta)
        {
            resultado.causa = CausaParada::Optima;
        }
        else
        {
            resultado.causa = CausaParada::SinSolucion;
        }

        resultado.nodos_creados = arena.nodos_creados();
        resultado.estados_distintos = tabla.size();
        resultado.memoria_arena = arena.memoria_reservada();
        resultado.memoria_abierta = abierta.memoria_reservada();
        resultado.memoria_tabla = tabla.memoria_reservada();

        const auto fin_reloj = std::chrono::steady_clock::now();
        resultado.milisegundos =
            std::chrono::duration<double, std::milli>(fin_reloj - inicio_reloj).count();

        return resultado;
    }

} // namespace referee_scheduler::algorithms
