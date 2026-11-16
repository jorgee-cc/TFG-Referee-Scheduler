// Construcción de los datos estáticos por día: este archivo declara las fases
// 1-5 (partición por fecha, compactación de árbitros, matriz de distancias,
// expansión de plazas y listas de candidatos). La búsqueda es por jornada y en
// paralelo entre días, así que cada grupo de la fase 1 es la entrada de las
// fases siguientes.
// La implementación está en `src/algorithms/construir_instancia.cpp`.

#pragma once

#include <cstddef>
#include <cstdint>
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

    /// Fase 3: compacta las ubicaciones del día y construye la matriz de tiempos.
    ///
    /// Las ubicaciones implicadas son las sedes de los partidos del día más los
    /// domicilios de sus árbitros (un domicilio nunca alberga un partido). Se
    /// ordenan por id global y se indexan `0..M-1`, igual que los árbitros, para
    /// que la matriz sea densa y quepa en el `sede_id` de 10 bits del estado
    /// (se exige `M <= 1024`).
    ///
    /// Se exige que las sedes de partido y los domicilios sean **conjuntos
    /// disjuntos** (`assert`): es lo que permite que, en el estado, estar en el
    /// domicilio signifique "todavía no ha salido de casa". Se pide la matriz
    /// completa al proveedor, se fuerza la diagonal a 0, todo desplazamiento
    /// entre ubicaciones distintas cuesta al menos `kMinutosViajeMinimo` (OSRM
    /// devuelve 0 entre puntos muy cercanos) y, si
    /// `opciones.cerrar_metricamente`, se cierra con Floyd-Warshall. También
    /// rellena `ArbitroDia::domicilio` con el índice compacto del domicilio de
    /// cada árbitro.
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

    /// Fase 4: expande los partidos del día en plazas ordenadas.
    ///
    /// Cada `domain::Plaza` se cruza con su `domain::Partido` para llevar al
    /// registro el horario, la sede compacta y los dos equipos. Solo entran las
    /// plazas de los partidos del día; las de otras jornadas se descartan (el
    /// partido de una plaza que no existe es un dato corrupto y lo debe validar
    /// el cargador).
    ///
    /// El resultado se ordena por `(minuto_inicio, sede, partido_id, plaza_id)`.
    /// El orden es **total** (los ids son únicos) y por tanto determinista, así
    /// que el índice de cada plaza es estable entre ejecuciones y sirve de nivel
    /// del árbol. La comparación usa la sede **compacta**, que ordena igual que
    /// el id global porque la fase 3 compacta las sedes por id ascendente.
    ///
    /// - `minuto_inicio` es el minuto del día del comienzo del partido.
    /// - `minuto_fin` es el minuto del día del final, medido en absoluto: un
    ///   partido de 23:30 a 01:00 acaba en el 1500, no recortado a 1440.
    /// - `retribucion_centimos` se obtiene una sola vez con `a_centimos`.
    ///
    /// @param sede_global Sedes compactadas de la fase 3, en orden ascendente;
    ///                    se usa para traducir `Partido::sede_id` a índice.
    std::vector<PlazaDia> construir_plazas(const domain::Instancia &instancia,
                                           const GrupoDia &grupo,
                                           const std::vector<int> &sede_global);

    /// Resultado de la fase 5: los candidatos de todas las plazas en un único
    /// bloque CSR, sin un vector por plaza.
    ///
    /// Un `vector<vector<...>>` tendría una cabecera de 24 B y una reserva
    /// separada por plaza; el CSR ocupa 4 B por candidato y se recorre en orden
    /// de memoria. Para la búsqueda basta con `offsets` + `candidatos`, que es lo
    /// que acaba copiado en `InstanciaDia`.
    struct ListasCandidatos
    {
        /// `n_plazas + 1` fronteras; la última es `candidatos.size()`.
        std::vector<std::uint32_t> offsets;

        /// Ids compactos de árbitro, plaza a plaza y en orden ascendente dentro
        /// de cada plaza.
        std::vector<std::uint32_t> candidatos;
    };

    /// Fase 5: construye las listas de candidatos de cada plaza.
    ///
    /// Recorre las plazas en su orden cronológico (el índice es el nivel del
    /// árbol) y para cada una filtra los árbitros del día con cuatro pruebas
    /// estáticas, todas conocidas antes de buscar:
    ///
    /// 1. **Nivel**: `ArbitroDia::nivel >= PlazaDia::nivel_requerido`.
    /// 2. **Ventana**: `primer_inicio <= minuto_inicio <= ultimo_inicio`. Se
    ///    compara con el **inicio** del partido, que es lo único que limitan la
    ///    disponibilidad declarada (`ultimo_inicio == kMinutosPorDia` significa
    ///    "sin límite" y siempre deja pasar). No se suma el viaje desde el
    ///    domicilio a `primer_inicio`: el primer desplazamiento del día puede
    ///    empezar antes de la disponibilidad (se sale de casa a
    ///    `inicio - viaje`), así que lo único exigible es que el partido no
    ///    empiece antes de `primer_inicio`. Es la misma condición que aplica
    ///    `coste_asignar` en el régimen "en el domicilio".
    /// 3. **Vetos**: el árbitro no puede tener vetado a ninguno de los dos
    ///    equipos del partido (`equipos_vetados` está ordenado y recortado a los
    ///    equipos del día, así que la prueba es una búsqueda binaria).
    /// 4. **Ruta**: `d(domicilio, sede) != kMinutosInalcanzable`. No hace falta
    ///    que el viaje quepa en ningún hueco; solo que exista camino, porque sin
    ///    él no hay forma de cubrir la plaza ni siquiera como primer partido.
    ///
    /// Que el árbitro llegue a tiempo desde donde esté en un nodo concreto
    /// depende del estado (sede actual y minuto de liberación) y **no** se puede
    /// decidir aquí: eso lo comprueba el generador de hijos con `coste_asignar`.
    ///
    /// El resultado incluye a los candidatos en orden ascendente de id compacto
    /// (se recorre `InstanciaDia::arbitros` en orden), de modo que el desempate
    /// es determinista. Una plaza sin candidatos deja los dos índices del CSR
    /// iguales; no es un error: el generador solo podrá dejarla vacía (prohibido
    /// si es principal, pagando su retribución si no lo es).
    ///
    /// @param dia Instancia con las fases 1-4 ya construidas.
    ListasCandidatos construir_candidatos(const InstanciaDia &dia);

    /// Construye la instancia de un día aplicando las fases 1-5.
    InstanciaDia construir_instancia_dia(
        const domain::Instancia &instancia,
        const GrupoDia &grupo,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones = {});

    /// Construye una `InstanciaDia` por cada fecha (fases 1-5).
    ///
    /// Ordenadas de la jornada más antigua a la más reciente. Cada una es
    /// independiente, así que se pueden resolver en paralelo.
    std::vector<InstanciaDia> construir_instancias(
        const domain::Instancia &instancia,
        ProveedorDistancias &proveedor,
        const OpcionesDistancias &opciones = {});

} // namespace referee_scheduler::algorithms
