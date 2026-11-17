// Paso 4/6 de la búsqueda: el motor de Dijkstra.
//
// Junta las tres piezas del paso 4:
//  - `ListaAbierta` (cubos por céntimo) como frontera;
//  - `TablaEstados` como lista cerrada, consultada **al generar** cada hijo;
//  - el generador de hijos del paso 3, que crea los nodos en la `Arena`.
//
// El objetivo del día es minimizar el tiempo (céntimos) sujeto a que las plazas
// principales se cubran; se modela como un camino de coste mínimo en un grafo
// por niveles, donde cada nivel es una plaza y cada arista es una asignación (o
// dejar vacía una plaza secundaria). Como todas las aristas tienen coste no
// negativo y el coste de asignar suma siempre `kEpsilonCentimos`, Dijkstra es
// correcto.
//
// Dos detalles que hacen que el bucle sea corto y correcto:
//  - la lista abierta extrae claves no decrecientes y la tabla solo reemplaza un
//    estado cuando el coste es **estrictamente** menor, así que un estado ya
//    extraído nunca mejora después (con `anchura_cubo == 1`, que es el valor por
//    defecto). No hay reaperturas: `kFlagCerrado` sirve para comprobarlo;
//  - al generar, el hijo que la tabla rechaza no entra en la lista abierta. El
//    nodo ya se ha creado en la arena, pero su subárbol no se explora nunca.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "referee_scheduler/algorithms/abierta.hpp"
#include "referee_scheduler/algorithms/arena.hpp"
#include "referee_scheduler/algorithms/instancia_dia.hpp"
#include "referee_scheduler/algorithms/objetivo.hpp"
#include "referee_scheduler/algorithms/tabla_estados.hpp"
#include "referee_scheduler/algorithms/transiciones.hpp"

namespace referee_scheduler::algorithms
{

    /// Límites y ajustes del motor de búsqueda.
    struct OpcionesBusqueda
    {
        /// Presupuesto de nodos del día: la búsqueda se detiene en cuanto la
        /// arena alcanza esta cifra.
        ///
        /// Es una red de seguridad frente a explosiones combinatorias, no un
        /// parámetro del algoritmo: al agotarlo, la solución encontrada (si la
        /// hay) **no está demostrada óptima**. La última expansión puede pasarse
        /// un poco del presupuesto, porque se comprueba una vez por expansión y
        /// no por hijo.
        std::size_t max_nodos = 25'000'000;

        /// Céntimos por cubo de la lista abierta.
        ///
        /// 1 (por defecto) da extracción exacta. Valores mayores agrupan claves
        /// para ahorrar memoria en días con muchas plazas secundarias vacías
        /// (cada una suma su retribución y estira el rango de claves); a cambio,
        /// la búsqueda explora más nodos, porque solo puede parar cuando el
        /// límite inferior del cubo actual ya no puede mejorar al incumbente.
        /// Nunca se pierde la optimalidad.
        std::int64_t anchura_cubo = 1;
    };

    /// Por qué terminó la búsqueda.
    enum class CausaParada
    {
        /// La búsqueda se agotó (o la cota ya no podía mejorar) y la solución
        /// hallada es óptima. Es el único final que demuestra optimalidad.
        Optima,

        /// No existe ninguna asignación factible: ni siquiera dejando vacías las
        /// plazas secundarias. Típicamente, una plaza principal sin candidatos.
        SinSolucion,

        /// Se agotó `OpcionesBusqueda::max_nodos`. Si `hay_solucion`, la solución
        /// es la mejor encontrada hasta ese momento, sin garantía de optimalidad.
        PresupuestoAgotado,
    };

    /// Resultado y métricas de una búsqueda de un día.
    struct ResultadoBusqueda
    {
        /// Por qué se detuvo la búsqueda.
        CausaParada causa = CausaParada::SinSolucion;

        /// ¿Se encontró alguna solución completa?
        bool hay_solucion = false;

        /// Coste de la solución en céntimos (0 si no hay solución).
        std::int64_t coste = 0;

        /// Nodo meta de la mejor solución, o `kSinNodo`.
        std::uint32_t meta = kSinNodo;

        /// Solución reconstruida (ids globales y decisiones por plaza). Coincide
        /// con `coste` en `solucion->coste`.
        std::optional<SolucionDia> solucion;

        // --- Métricas (para depurar, comparar y decidir optimizaciones) ---

        /// Nodos que hay en la arena al terminar (incluye la raíz).
        std::size_t nodos_creados = 0;

        /// Nodos extraídos de la lista abierta y expandidos (incluye metas).
        std::size_t nodos_expandidos = 0;

        /// Hijos generados que no entraron en la lista abierta: rechazados por la
        /// tabla (estado ya visto con coste igual o mejor) o por la cota del
        /// incumbente.
        std::size_t hijos_descartados = 0;

        /// Nodos extraídos con `kFlagSuperado` (los sustituyó un camino mejor).
        /// Con `anchura_cubo == 1` son entradas que quedaron obsoletas en la
        /// lista abierta, no reaperturas.
        std::size_t extracciones_obsoletas = 0;

        /// Estados distintos registrados en la tabla.
        std::size_t estados_distintos = 0;

        /// Máximo de nodos simultáneos en la lista abierta.
        std::size_t max_abierta = 0;

        /// Bytes comprometidos por la arena al terminar.
        std::size_t memoria_arena = 0;

        /// Bytes comprometidos por la lista abierta al terminar.
        std::size_t memoria_abierta = 0;

        /// Bytes comprometidos por la tabla de estados al terminar.
        std::size_t memoria_tabla = 0;

        /// Tiempo de pared del motor de búsqueda (sin contar la construcción de
        /// los datos estáticos del día).
        double milisegundos = 0.0;
    };

    /// Resuelve un día con Dijkstra sobre el árbol de decisiones.
    ///
    /// El grafo se explora nivel a nivel (una plaza por nivel) desde la raíz que
    /// crea `crear_raiz`. Devuelve la solución de coste mínimo o el motivo por el
    /// que no la hay. El coste de la meta ya incluye todos los viajes, esperas,
    /// retornos y las retribuciones de las plazas secundarias que se dejan vacías.
    ///
    /// @param arena Arena **vacía** construida con `dia.n_arbitros()` árbitros;
    ///              es donde se crean todos los nodos de la búsqueda.
    /// @param dia Instancia del día con las fases 1–5 construidas.
    /// @param parametros Pesos de la función objetivo.
    /// @param opciones Presupuesto de nodos y anchura de los cubos.
    /// @return Resultado con la solución y las métricas.
    /// @pre `arena.nodos_creados() == 0`, `dia.n_arbitros() > 0` y
    ///      `arena.n_arbitros() == dia.n_arbitros()`.
    ResultadoBusqueda buscar_dijkstra(Arena &arena,
                                      const InstanciaDia &dia,
                                      const ParametrosObjetivo &parametros = {},
                                      const OpcionesBusqueda &opciones = {});

} // namespace referee_scheduler::algorithms
