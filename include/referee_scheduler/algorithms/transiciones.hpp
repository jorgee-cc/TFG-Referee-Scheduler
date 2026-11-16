// Paso 3 de la búsqueda: estado inicial, nodo raíz y generación de hijos.
//
// La cabecera de cada nodo guarda la decisión que lo creó (`CabeceraNodo::
// decision`), de modo que reconstruir la solución es recorrer la cadena de
// padres, sin comparar estados. El generador no decide el orden de exploración:
// solo crea nodos en la `Arena` y entrega sus ids; la frontera es el paso 4.
//
// Hay dos formas de recoger los hijos:
//  - `generar_hijos(..., std::vector<uint32_t>&)`, cómoda y sin asignaciones
//    repetidas si el llamador reutiliza el vector;
//  - `generar_hijos(..., consumidor)`, que invoca el callback directamente y
//    evita el vector intermedio. Es la variante pensada para optimizaciones
//    futuras de la frontera.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "referee_scheduler/algorithms/arena.hpp"
#include "referee_scheduler/algorithms/instancia_dia.hpp"
#include "referee_scheduler/algorithms/objetivo.hpp"

namespace referee_scheduler::algorithms
{

    /// Construye el estado inicial de una jornada.
    ///
    /// Cada árbitro compacto `i` queda:
    ///  - en su domicilio (`ArbitroDia::domicilio`), que nunca es una sede de
    ///    partido: así `coste_asignar` reconoce que todavía no ha salido de casa;
    ///  - libre desde `ArbitroDia::primer_inicio`, el primer minuto en que puede
    ///    **arbitrar** (no la hora de salida);
    ///  - con los bits reservados a cero.
    ///
    /// Las ranuras `[n_arbitros, kMaxArbitros)` quedan a cero; la arena no las
    /// copia, pero mantenerlas limpias hace que el estado sea reproducible.
    ///
    /// @param dia Instancia del día con las fases 1–5 construidas. Solo se leen
    ///            `arbitros`, `sede_global` y `n_sedes()`.
    /// @return Estado de capacidad máxima; solo las `n_arbitros()` primeras
    ///         entradas son significativas.
    /// @pre `dia.n_arbitros() <= kMaxArbitros` y, si hay árbitros, cada uno
    ///      tiene un `domicilio` compacto válido.
    Estado estado_inicial(const InstanciaDia &dia);

    /// Crea el nodo raíz de la búsqueda de un día.
    ///
    /// La arena debe haberse construido con `dia.n_arbitros()`, que además debe
    /// ser mayor que cero (la arena no representa días sin árbitros). La raíz
    /// tiene coste 0, nivel 0, padre `kSinNodo` y `decision = kSinDecision`.
    ///
    /// @param arena Arena nueva, aún sin nodos, del mismo `n_arbitros` que `dia`.
    /// @param dia Instancia del día.
    /// @return Índice del nodo raíz, que siempre es 0 en una arena nueva.
    /// @pre `arena.nodos_creados() == 0` y `dia.n_arbitros() > 0`.
    std::uint32_t crear_raiz(Arena &arena, const InstanciaDia &dia);

    /// ¿El nodo ha decidido ya todas las plazas?
    ///
    /// Cuando `nivel == n_plazas()` el nodo es una solución completa y su
    /// `coste_g` ya incluye viajes, esperas, retornos y plazas vacías.
    ///
    /// @param arena Arena que contiene el nodo.
    /// @param dia Instancia del día.
    /// @param nodo Nodo a comprobar; debe existir.
    /// @return `true` si no quedan plazas por decidir.
    bool es_meta(const Arena &arena, const InstanciaDia &dia, std::uint32_t nodo);

    /// Condición **necesaria pero no suficiente** para que el día tenga solución.
    ///
    /// Toda plaza principal debe tener al menos un candidato estático. No
    /// comprueba la factibilidad dinámica (que el árbitro llegue a tiempo desde
    /// su sede actual): eso solo lo sabe el generador de hijos.
    ///
    /// Sirve para rechazar antes de construir la arena los días imposibles por
    /// nivel, ventana, vetos o falta de ruta. Los días sin árbitros y con solo
    /// plazas secundarias son válidos: la solución es dejarlas todas vacías.
    ///
    /// @param dia Instancia del día con el CSR de candidatos construido.
    /// @return `false` si alguna principal no tiene ningún candidato.
    bool tiene_solucion_estatica(const InstanciaDia &dia) noexcept;

    /// Decisión reconstruida de una plaza, ya traducida a ids globales.
    ///
    /// `InstanciaDia` trabaja internamente con índices compactos; este tipo es
    /// la frontera con el resultado del problema y conserva los ids originales.
    struct DecisionDia
    {
        /// Id global de la plaza (`Plaza::id`).
        int plaza_id = 0;

        /// Id global del partido (`Partido::id`).
        int partido_id = 0;

        /// Id global del árbitro asignado, o `-1` si la plaza quedó vacía.
        int arbitro_global = -1;
    };

    /// Solución completa de una jornada.
    ///
    /// `coste` es el `coste_g` de la meta; ya incluye todos los viajes, esperas,
    /// retornos y retribuciones perdidas. `decisiones` tiene exactamente
    /// `n_plazas()` entradas, en el mismo orden cronológico que
    /// `InstanciaDia::plazas`.
    struct SolucionDia
    {
        /// Coste acumulado de la meta, en céntimos.
        std::int64_t coste = 0;

        /// Una decisión por plaza, en el orden cronológico de `InstanciaDia::plazas`.
        std::vector<DecisionDia> decisiones;
    };

    /// Reconstruye la solución subiendo por la cadena de padres desde `meta`.
    ///
    /// No compara estados: lee `CabeceraNodo::decision` en cada nodo. Un nodo
    /// con `nivel = p + 1` guarda la decisión de `plazas[p]`. La salida queda en
    /// orden cronológico y con los ids globales listos para el resultado final.
    ///
    /// @param arena Arena que contiene la cadena de nodos.
    /// @param dia Instancia del día.
    /// @param meta Nodo completo (`es_meta(arena, dia, meta) == true`).
    /// @return Coste de la meta y una decisión por plaza.
    SolucionDia reconstruir_solucion(const Arena &arena,
                                     const InstanciaDia &dia,
                                     std::uint32_t meta);

    namespace detalle
    {

        /// Núcleo del generador: recorre candidatos y rama vacía invocando
        /// `consumir` por cada hijo. No usa ningún contenedor intermedio.
        ///
        /// Para cada candidato `c` de la plaza actual:
        ///  1. lee su registro del estado del padre (4 B);
        ///  2. pide a la matriz `d(sede_actual, sede_plaza)`,
        ///     `d(sede_plaza, domicilio)` y `d(sede_actual, domicilio)`;
        ///  3. llama a `coste_asignar`, que decide factibilidad y coste;
        ///  4. copia el estado del padre, parchea solo el registro `c` con
        ///     (`sede_plaza`, `minuto_fin`) y crea el nodo.
        ///
        /// Después intenta la rama de plaza vacía. La principal no la tiene:
        /// `coste_dejar_vacia` devuelve `nullopt` y el nodo se queda sin hijos.
        ///
        /// Precondiciones (se comprueban con `assert` en depuración):
        ///  - `nodo` existe en la arena y no es meta (`nivel < n_plazas()`);
        ///  - la arena representa los mismos árbitros que `dia`;
        ///  - la matriz de distancias está completa.
        ///
        /// La matriz debe haberse construido con cierre métrico y todas las
        /// celdas usadas deben ser finitas. Si alguna distancia es
        /// `kMinutosInalcanzable`, el hijo no se crea.
        ///
        /// `consumir` se invoca exactamente una vez por hijo y en el mismo orden
        /// en que los nodos se crean en la arena: asignaciones ascendentes y
        /// después la plaza vacía. No se limpia ni se reserva nada en el
        /// consumidor: eso es responsabilidad de quien lo proporciona.
        ///
        /// @param arena Arena donde se crean los nodos hijos.
        /// @param dia Instancia del día con las fases 1–5 construidas.
        /// @param nodo Nodo padre que se expande.
        /// @param parametros Pesos de la función objetivo.
        /// @param consumir Callable que acepta `std::uint32_t` por cada hijo.
        template <typename Consumidor>
        void generar_hijos_impl(Arena &arena,
                                const InstanciaDia &dia,
                                std::uint32_t nodo,
                                const ParametrosObjetivo &parametros,
                                Consumidor &consumir)
        {
            assert(arena.n_arbitros() == dia.n_arbitros());
            assert(nodo < arena.nodos_creados());
            assert(dia.n_arbitros() <= kMaxArbitros);

            const CabeceraNodo cab = arena.cabecera(nodo);
            assert(cab.nivel < dia.n_plazas() &&
                   "no se generan hijos de una solucion completa");

            const std::size_t nivel = cab.nivel;
            const PlazaDia &plaza = dia.plazas[nivel];
            const std::span<const EstadoArbitro> estado_padre = arena.estado(nodo);

            // `plaza.minuto_fin` se escribe en los 11 bits de
            // `EstadoArbitro::minuto_liberacion`; la fase 4 ya lo garantiza,
            // pero el generador deja la precondición escrita.
            assert(plaza.minuto_fin >= 0 && plaza.minuto_fin < 2048);
            assert(plaza.sede < dia.n_sedes());

            // 1. Asignaciones, en orden ascendente de id compacto de árbitro.
            //    La fase 5 ya filtró nivel, ventana, vetos y ruta de ida y
            //    vuelta desde el domicilio; aquí solo queda la factibilidad
            //    dinámica (llegar desde la sede actual).
            for (const std::uint32_t arbitro_id : dia.candidatos_de(nivel))
            {
                assert(arbitro_id < dia.n_arbitros());
                const ArbitroDia &arbitro = dia.arbitros[arbitro_id];
                const EstadoArbitro actual = estado_padre[arbitro_id];

                assert(arbitro.domicilio < dia.n_sedes());

                const int viaje = dia.viaje(actual.sede_id, plaza.sede);
                const int retorno_nuevo = dia.viaje(plaza.sede, arbitro.domicilio);
                const int retorno_anterior =
                    dia.viaje(actual.sede_id, arbitro.domicilio);

                // Ninguna transición puede construirse con una distancia
                // imposible: el sentinela rompería el ajuste telescópico del
                // retorno y la garantía de coste no negativo.
                if (viaje == kMinutosInalcanzable ||
                    retorno_nuevo == kMinutosInalcanzable ||
                    retorno_anterior == kMinutosInalcanzable)
                {
                    continue;
                }

                const auto coste = coste_asignar(
                    parametros, actual, static_cast<int>(arbitro.domicilio),
                    plaza.minuto_inicio, viaje, retorno_nuevo, retorno_anterior);

                if (!coste)
                {
                    // No llega a tiempo o empieza antes de su disponibilidad
                    // (el segundo caso ya lo descarta la fase 5, pero la
                    // función objetivo es la última defensa).
                    continue;
                }

                CabeceraNodo cab_hijo;
                cab_hijo.coste_g = cab.coste_g + *coste;
                cab_hijo.padre = nodo;
                cab_hijo.nivel = cab.nivel + 1;
                cab_hijo.flags = 0;
                cab_hijo.decision = arbitro_id;

                // Se crea el hijo copiando el estado del padre y se parchea
                // solo el registro del árbitro asignado. Es una única copia de
                // 4·n_arbitros bytes, sin búfer intermedio.
                const std::uint32_t hijo = arena.crear(estado_padre, cab_hijo);

                EstadoArbitro nuevo = actual;
                nuevo.sede_id = plaza.sede;
                nuevo.minuto_liberacion =
                    static_cast<std::uint32_t>(plaza.minuto_fin);
                nuevo.reservado = 0;
                arena.estado(hijo)[arbitro_id] = nuevo;

                consumir(hijo);
            }

            // 2. Rama de dejar la plaza vacía. Las principales no la tienen
            //    (`coste_dejar_vacia` devuelve `nullopt`), así que un nodo con
            //    una principal imposible se queda sin hijos y muere.
            if (const auto coste = coste_dejar_vacia(plaza.rol,
                                                     plaza.retribucion_centimos))
            {
                CabeceraNodo cab_hijo;
                cab_hijo.coste_g = cab.coste_g + *coste;
                cab_hijo.padre = nodo;
                cab_hijo.nivel = cab.nivel + 1;
                cab_hijo.flags = 0;
                cab_hijo.decision = kDecisionVacia;

                consumir(arena.crear(estado_padre, cab_hijo));
            }
        }

    } // namespace detalle

    /// Genera los hijos de `nodo` y los entrega uno a uno a `consumir`.
    ///
    /// Es la variante sin vector intermedio: el consumidor puede insertar
    /// directamente en la frontera o contar hijos sin reservar memoria. El
    /// orden es determinista: primero las asignaciones en orden ascendente y
    /// después la rama vacía.
    ///
    /// @param arena Arena donde se crean los hijos.
    /// @param dia Instancia del día.
    /// @param nodo Nodo padre; debe existir y no ser meta.
    /// @param parametros Pesos de la función objetivo.
    /// @param consumir Callable invocado con cada id de hijo creado.
    template <typename Consumidor>
    void generar_hijos(Arena &arena,
                       const InstanciaDia &dia,
                       std::uint32_t nodo,
                       const ParametrosObjetivo &parametros,
                       Consumidor &&consumir)
    {
        detalle::generar_hijos_impl(arena, dia, nodo, parametros, consumir);
    }

    /// Genera los hijos de `nodo` y los deja en `hijos`.
    ///
    /// Limpia `hijos` al entrar. Mantiene el mismo orden determinista que la
    /// variante con callback. Reutilizar el mismo vector entre expansiones evita
    /// reservas repetidas.
    ///
    /// @param arena Arena donde se crean los hijos.
    /// @param dia Instancia del día.
    /// @param nodo Nodo padre; debe existir y no ser meta.
    /// @param parametros Pesos de la función objetivo.
    /// @param hijos Vector de salida; se limpia antes de rellenarlo.
    void generar_hijos(Arena &arena,
                       const InstanciaDia &dia,
                       std::uint32_t nodo,
                       const ParametrosObjetivo &parametros,
                       std::vector<std::uint32_t> &hijos);

} // namespace referee_scheduler::algorithms
