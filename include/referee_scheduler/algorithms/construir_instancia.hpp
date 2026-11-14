// Construcción de los datos estáticos por día: este archivo declara la fase 1,
// que parte la `Instancia` completa en grupos por fecha civil (`GrupoDia`).
// La búsqueda es por jornada y en paralelo entre días, así que cada grupo es la
// entrada de las fases siguientes (compactación de árbitros y sedes, matriz de
// distancias, plazas y listas de candidatos).
// La implementación está en `src/algorithms/construir_instancia.cpp`.

#pragma once

#include <cstddef>
#include <vector>

#include "referee_scheduler/algorithms/instancia_dia.hpp"
#include "referee_scheduler/algorithms/proveedor_distancias.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace referee_scheduler::algorithms
{

    /// Partidos de una misma fecha civil: primer producto de los datos estáticos.
    ///
    /// La instancia de entrada cubre varias jornadas (viernes, sábado y domingo,
    /// posiblemente más). La búsqueda es **por día**: cada fecha genera una
    /// instancia independiente que se puede resolver en paralelo. Este tipo es
    /// el resultado de partir la entrada por fecha; las fases siguientes
    /// (compactación de árbitros y sedes, matriz de distancias, plazas y
    /// candidatos) consumen un `GrupoDia` cada una.
    struct GrupoDia
    {
        /// Fecha civil local a la que pertenecen todos los partidos del grupo.
        DiaCivil dia;

        /// Posiciones en `Instancia::partidos` de los partidos que empiezan ese
        /// día, en el orden de entrada.
        ///
        /// Se guardan índices y no copias para no duplicar los datos de entrada.
        /// El partido pertenece al día en que **empieza**: un encuentro del
        /// viernes a las 23:30 que termina el sábado a la 01:00 sigue siendo del
        /// viernes, aunque su `final` caiga en la fecha siguiente.
        std::vector<std::size_t> partidos;
    };

    /// Fase 1: parte la entrada por fecha civil.
    ///
    /// Devuelve un grupo por cada fecha distinta que tenga al menos un partido,
    /// ordenados del día más antiguo al más reciente. Dentro de cada grupo se
    /// respeta el orden de entrada de los partidos (que además es el orden de
    /// recorrido de `plazas`, porque una plaza referencia a su partido por id).
    ///
    /// `Partido::inicio` se descompone en hora local con las utilidades de
    /// `tiempo.hpp`, nunca restando `time_t`, para que un cambio de huso o de
    /// horario de verano no mezcle dos jornadas.
    std::vector<GrupoDia> agrupar_por_dia(const domain::Instancia &instancia);

    /// Fase 2: compacta los árbitros de un día a `0..N_dia-1`.
    ///
    /// Un árbitro entra en la jornada si su ventana de disponibilidad contiene
    /// algún minuto de **inicio** válido del día (de 00:00 a 23:59). Es decir, se
    /// compara el intervalo absoluto `[disponibilidad_inicio,
    /// disponibilidad_fin]` con el día completo, no se exige que la contenga:
    /// quien solo puede arbitrar un partido nocturno también debe entrar.
    ///
    /// La ventana se interpreta sobre la hora de inicio: estar disponible hasta
    /// las 23:00 significa poder empezar un partido a las 23:00, aunque acabe
    /// después de medianoche.
    ///
    /// El resultado sale ordenado por `id_global` ascendente, de modo que la
    /// asignación de ids compactos es determinista y no depende del orden en que
    /// el cargador haya metido los árbitros. El índice en el vector devuelto es
    /// el identificador compacto del árbitro.
    ///
    /// Los vetos se recortan a los equipos que juegan ese día (el resto no puede
    /// aparecer en ninguna plaza) y se ordenan para el filtro de candidatos.
    std::vector<ArbitroDia> compactar_arbitros(const domain::Instancia &instancia,
                                               const GrupoDia &grupo);

    /// Opciones de la fase 3 (matriz de distancias).
    struct OpcionesDistancias
    {
        /// Aplica el cierre métrico (Floyd-Warshall) a la matriz.
        ///
        /// La función objetivo necesita que `d(a,c) <= d(a,b) + d(b,c)` para que
        /// ninguna transición abarate el camino y Dijkstra siga siendo válido.
        /// OSRM devuelve el tiempo de la ruta más rápida, que en la práctica
        /// casi siempre cumple la desigualdad, pero no está garantizado. El
        /// cierre la impone (con coste `O(M^3)`, despreciable para M ~ 100) sin
        /// inventar rutas: solo sustituye una celda por un camino compuesto más
        /// corto.
        bool cerrar_metricamente = true;
    };

    /// Sedes compactadas de un día y la matriz de tiempos entre ellas.
    struct MatrizDistancias
    {
        /// Id global de cada sede compacta, en orden ascendente.
        std::vector<int> sede_global;
        /// Matriz `n × n` de minutos en orden row-major.
        std::vector<Minutos> valores;
    };

    /// Fase 3: compacta las sedes del día y construye la matriz de tiempos.
    ///
    /// Las sedes implicadas son las de los partidos del día más los domicilios
    /// de sus árbitros (un árbitro puede vivir en una sede que no albergue
    /// ningún partido). Se ordenan por id global y se indexan `0..M-1`, igual
    /// que los árbitros, para que la matriz sea densa y quepa en el `sede_id`
    /// de 10 bits del estado (se exige `M <= 1024`).
    ///
    /// Se pide la matriz completa al proveedor, se fuerza la diagonal a 0 y,
    /// si `opciones.cerrar_metricamente`, se cierra con Floyd-Warshall. También
    /// rellena `ArbitroDia::sede_domicilio` con el índice compacto del
    /// domicilio de cada árbitro.
    ///
    /// @param arbitros Árbitros ya compactados (fase 2); se modifican in situ.
    MatrizDistancias construir_matriz_distancias(
        const domain::Instancia &instancia,
        const GrupoDia &grupo,
        std::vector<ArbitroDia> &arbitros,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones = {});

    /// Impone la desigualdad triangular a una matriz de minutos.
    ///
    /// Floyd-Warshall in situ: para cada par (`i`, `j`) deja el camino más
    /// corto conocido, de modo que `d[i][j] <= d[i][k] + d[k][j]` para toda `k`.
    /// Las parejas sin ruta (`kMinutosInalcanzable`) no se usan como puente ni
    /// se rellenan: si no hay camino, siguen inalcanzables.
    ///
    /// @param distancias `n × n` valores row-major; se modifican.
    void cerrar_metricamente(std::vector<Minutos> &distancias,
                             std::size_t n_sedes);

    /// Construye la instancia de un día aplicando las fases 1-3.
    ///
    /// Las plazas (fase 4) y las listas de candidatos (fase 5) se añadirán a
    /// esta misma función cuando existan.
    InstanciaDia construir_instancia_dia(
        const domain::Instancia &instancia,
        const GrupoDia &grupo,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones = {});

    /// Construye una `InstanciaDia` por cada fecha (fases 1-3).
    ///
    /// Ordenadas de la jornada más antigua a la más reciente. Cada una es
    /// independiente, así que se pueden resolver en paralelo.
    std::vector<InstanciaDia> construir_instancias(
        const domain::Instancia &instancia,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones = {});

} // namespace referee_scheduler::algorithms
